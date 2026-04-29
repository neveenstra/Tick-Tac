#pragma once
#include <stdint.h>

void midi_clock_init(void);
void midi_clock_start(void);
void midi_clock_stop(void);
void midi_clock_set_tempo(uint16_t bpm);
