#pragma once
#include <stdint.h>

void    midi_clock_init(void);
void    midi_clock_start(void);
void    midi_clock_stop(void);
void    midi_clock_set_tempo(uint16_t bpm);

// CV-sync PPQ: cycles per quarter note (square wave). Allowed: 1, 2, 4, 24, 48.
void    midi_clock_ppq_step(int delta);   // +1 = next higher, -1 = next lower
uint8_t midi_clock_get_ppq(void);
