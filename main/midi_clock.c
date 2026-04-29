#include "midi_clock.h"
#include "driver/uart.h"
#include "esp_timer.h"

// 31250 baud UART MIDI out on GPIO4 (free pin, not used by display/touch/IMU)
#define MIDI_UART   UART_NUM_1
#define MIDI_TX_PIN 4
#define MIDI_BAUD   31250
#define MIDI_PPQN   24   // 24 clocks per quarter note

static esp_timer_handle_t s_timer;
static volatile uint32_t  s_interval_us;
static volatile bool      s_running;

static void send_byte(uint8_t b)
{
    uart_write_bytes(MIDI_UART, (const char *)&b, 1);
}

// One-shot timer: fires, sends 0xF8, re-arms itself
static void clock_cb(void *arg)
{
    if (!s_running) return;
    send_byte(0xF8); // MIDI Clock
    esp_timer_start_once(s_timer, s_interval_us);
}

void midi_clock_init(void)
{
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

    const esp_timer_create_args_t args = {
        .callback = clock_cb,
        .name     = "midi_clk",
    };
    esp_timer_create(&args, &s_timer);

    s_interval_us = 60000000UL / ((uint32_t)120 * MIDI_PPQN);
    s_running     = false;
}

void midi_clock_start(void)
{
    if (s_running) return;
    s_running = true;
    send_byte(0xFA); // MIDI Start
    esp_timer_start_once(s_timer, s_interval_us);
}

void midi_clock_stop(void)
{
    if (!s_running) return;
    s_running = false;
    esp_timer_stop(s_timer);
    send_byte(0xFC); // MIDI Stop
}

void midi_clock_set_tempo(uint16_t bpm)
{
    if (bpm < 20 || bpm > 300) return;
    // 32-bit aligned write is atomic on single-core RISC-V; change takes effect on next re-arm
    s_interval_us = 60000000UL / ((uint32_t)bpm * MIDI_PPQN);
}
