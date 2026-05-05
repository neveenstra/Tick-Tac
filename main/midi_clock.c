#include "midi_clock.h"
#include "usb_midi.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs.h"

// NVS storage for the PPQ setting so it survives a power cycle.
#define NVS_NAMESPACE "midi"
#define NVS_KEY_PPQ   "ppq_idx"

// 31250 baud UART MIDI out on GPIO4
#define MIDI_UART       UART_NUM_1
#define MIDI_TX_PIN     4
#define MIDI_BAUD       31250
#define MIDI_PPQN       24       // MIDI standard: 24 clock messages per quarter note
#define BASE_PPQN       96       // Internal timer rate; 4 ticks per MIDI clock,
                                 // and divides evenly for CV PPQ values 1,2,4,24,48
#define TICKS_PER_MIDI  (BASE_PPQN / MIDI_PPQN)   // = 4

// CV sync: 3.3V square wave on GPIO 5. PPQ is cycles per quarter note.
// Idle = HIGH so a stopped clock doesn't look like a held trigger / latched note.
#define CV_SYNC_PIN     GPIO_NUM_5
#define CV_IDLE_LEVEL   1

static const uint8_t PPQ_VALUES[] = {1, 2, 4, 24, 48};
#define PPQ_COUNT (sizeof(PPQ_VALUES) / sizeof(PPQ_VALUES[0]))

static esp_timer_handle_t s_timer;
static volatile uint32_t  s_interval_us;       // base-tick interval (1/96 of a quarter note)
static volatile bool      s_running;
static bool               s_cv_state;
static uint32_t           s_tick;              // counts base ticks while running
static uint8_t            s_ppq_idx = 3;       // start at 24 PPQ (DIN sync default)

static void send_byte(uint8_t b)
{
    uart_write_bytes(MIDI_UART, (const char *)&b, 1);
}

static void ppq_load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t idx = s_ppq_idx;
    if (nvs_get_u8(h, NVS_KEY_PPQ, &idx) == ESP_OK && idx < PPQ_COUNT) {
        s_ppq_idx = idx;
    }
    nvs_close(h);
}

static void ppq_save_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, NVS_KEY_PPQ, s_ppq_idx) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

// Periodic timer fires BASE_PPQN times per quarter note. Period anchored to
// the original start time, so callback duration doesn't drift the clock.
static void clock_cb(void *arg)
{
    if (!s_running) return;

    // MIDI clock byte every TICKS_PER_MIDI base ticks
    if ((s_tick % TICKS_PER_MIDI) == 0) {
        send_byte(0xF8);
        usb_midi_send_byte(0xF8);
    }

    // CV: square wave with PPQ cycles per quarter = 2*PPQ edges per quarter.
    // Toggle every (BASE_PPQN / (2*PPQ)) ticks = (48 / PPQ) ticks.
    uint8_t  ppq           = PPQ_VALUES[s_ppq_idx];
    uint32_t toggle_period = 48u / ppq;
    if ((s_tick % toggle_period) == 0) {
        s_cv_state = !s_cv_state;
        gpio_set_level(CV_SYNC_PIN, s_cv_state);
    }

    s_tick++;
}

void midi_clock_init(void)
{
    // UART MIDI
    uart_config_t cfg = {
        .baud_rate  = MIDI_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(MIDI_UART, &cfg);
    uart_set_pin(MIDI_UART, MIDI_TX_PIN, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(MIDI_UART, 256, 0, 0, NULL, 0);

    // CV sync GPIO
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << CV_SYNC_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(CV_SYNC_PIN, CV_IDLE_LEVEL);

    const esp_timer_create_args_t clock_args = {
        .callback = clock_cb,
        .name     = "midi_clk",
    };
    esp_timer_create(&clock_args, &s_timer);

    s_interval_us = 60000000UL / ((uint32_t)120 * BASE_PPQN);
    s_running     = false;
    s_cv_state    = CV_IDLE_LEVEL;
    s_tick        = 0;

    ppq_load_from_nvs();
}

void midi_clock_start(void)
{
    if (s_running) return;
    // Anchor toggle to idle so the first tick produces a clean idle→active edge.
    s_cv_state = CV_IDLE_LEVEL;
    s_tick     = 0;
    s_running  = true;
    send_byte(0xFA);
    usb_midi_send_byte(0xFA);
    esp_timer_start_periodic(s_timer, s_interval_us);
}

void midi_clock_stop(void)
{
    if (!s_running) return;
    s_running = false;
    esp_timer_stop(s_timer);
    gpio_set_level(CV_SYNC_PIN, CV_IDLE_LEVEL);
    s_cv_state = CV_IDLE_LEVEL;
    send_byte(0xFC);
    usb_midi_send_byte(0xFC);
}

void midi_clock_set_tempo(uint16_t bpm)
{
    if (bpm < 20 || bpm > 300) return;
    s_interval_us = 60000000UL / ((uint32_t)bpm * BASE_PPQN);
    if (s_running) {
        esp_timer_stop(s_timer);
        esp_timer_start_periodic(s_timer, s_interval_us);
    }
}

void midi_clock_ppq_step(int delta)
{
    int next = (int)s_ppq_idx + delta;
    if (next < 0) next = 0;
    if (next > (int)PPQ_COUNT - 1) next = PPQ_COUNT - 1;
    if ((uint8_t)next == s_ppq_idx) return;   // no change, skip the NVS write
    s_ppq_idx = (uint8_t)next;
    ppq_save_to_nvs();
}

uint8_t midi_clock_get_ppq(void)
{
    return PPQ_VALUES[s_ppq_idx];
}
