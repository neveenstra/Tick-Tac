#include "midi_clock.h"
#include "usb_midi.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// 31250 baud UART MIDI out on GPIO4 (free pin, not used by display/touch/IMU)
#define MIDI_UART       UART_NUM_1
#define MIDI_TX_PIN     4
#define MIDI_BAUD       31250
#define MIDI_PPQN       24   // 24 clocks per quarter note

// CV sync: 3.3V pulse on GPIO38, one pulse per quarter note (every 24 clocks)
#define CV_SYNC_PIN         GPIO_NUM_5
#define CV_SYNC_PULSE_US    5000    // 5 ms pulse width

static esp_timer_handle_t s_timer;
static esp_timer_handle_t s_cv_timer;
static volatile uint32_t  s_interval_us;
static volatile bool      s_running;
static uint8_t            s_clock_count;   // 0..MIDI_PPQN-1

static void send_byte(uint8_t b)
{
    uart_write_bytes(MIDI_UART, (const char *)&b, 1);
}

static void cv_off_cb(void *arg)
{
    gpio_set_level(CV_SYNC_PIN, 0);
}

// Periodic timer fires every s_interval_us. Period is anchored to the original
// start time, so callback duration doesn't drift the clock.
static void clock_cb(void *arg)
{
    if (!s_running) return;

    send_byte(0xF8);        // UART MIDI Clock
    usb_midi_send_byte(0xF8);

    if (s_clock_count == 0) {
        gpio_set_level(CV_SYNC_PIN, 1);
        esp_timer_start_once(s_cv_timer, CV_SYNC_PULSE_US);
    }
    if (++s_clock_count >= MIDI_PPQN) s_clock_count = 0;
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

    // Clock timer
    const esp_timer_create_args_t clock_args = {
        .callback = clock_cb,
        .name     = "midi_clk",
    };
    esp_timer_create(&clock_args, &s_timer);

    // CV pulse-off timer
    const esp_timer_create_args_t cv_args = {
        .callback = cv_off_cb,
        .name     = "cv_off",
    };
    esp_timer_create(&cv_args, &s_cv_timer);

    s_interval_us = 60000000UL / ((uint32_t)120 * MIDI_PPQN);
    s_running     = false;
    s_clock_count = 0;
}

void midi_clock_start(void)
{
    if (s_running) return;
    s_clock_count = 0;
    s_running = true;
    send_byte(0xFA);
    usb_midi_send_byte(0xFA);
    esp_timer_start_periodic(s_timer, s_interval_us);
}

void midi_clock_stop(void)
{
    if (!s_running) return;
    s_running = false;
    esp_timer_stop(s_timer);
    esp_timer_stop(s_cv_timer);
    gpio_set_level(CV_SYNC_PIN, 0);
    send_byte(0xFC);
    usb_midi_send_byte(0xFC);
}

void midi_clock_set_tempo(uint16_t bpm)
{
    if (bpm < 20 || bpm > 300) return;
    s_interval_us = 60000000UL / ((uint32_t)bpm * MIDI_PPQN);
    if (s_running) {
        esp_timer_stop(s_timer);
        esp_timer_start_periodic(s_timer, s_interval_us);
    }
}
