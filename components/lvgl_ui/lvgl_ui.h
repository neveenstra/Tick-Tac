#ifndef __LVGL_UI_H__
#define __LVGL_UI_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void     lvgl_ui_init(void);
uint16_t lvgl_ui_get_tempo(void);
void     lvgl_ui_set_tempo(uint16_t tempo);
bool     lvgl_ui_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif
