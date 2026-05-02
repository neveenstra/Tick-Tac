#include "usb_midi.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/midi/midi_device.h"

enum { ITF_NUM_MIDI = 0, ITF_NUM_MIDI_STREAMING, ITF_COUNT };
enum { EP_EMPTY = 0, EPNUM_MIDI };

#define TUSB_DESCRIPTOR_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const char *s_str_desc[] = {
    (char[]){0x09, 0x04},   // 0: English (0x0409)
    "Crazysoap inc.",       // 1: Manufacturer
    "Tick-Tac",             // 2: Product
    "TICKTAC0001",          // 3: Serial
    "MIDI Clock",           // 4: MIDI interface
};

static const uint8_t s_midi_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 64),
};

void usb_midi_init(void)
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.string       = s_str_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_str_desc) / sizeof(s_str_desc[0]);
    tusb_cfg.descriptor.full_speed_config = s_midi_cfg_desc;
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
}

void usb_midi_send_byte(uint8_t b)
{
    if (!tud_midi_mounted()) return;
    tud_midi_stream_write(0, &b, 1);
}
