#pragma once
#include <stdint.h>

void usb_midi_init(void);
void usb_midi_send_byte(uint8_t b);
