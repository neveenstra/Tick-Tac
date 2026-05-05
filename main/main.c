#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lvgl_port.h"
#include "esp_timer.h"

#include "bsp_display.h"
#include "bsp_touch.h"
#include "bsp_i2c.h"
#include "midi_clock.h"
#include "usb_midi.h"

#include "lvgl_ui.h"

#define DISPLAY_ROTATION 90
#define LCD_H_RES (320)
#define LCD_V_RES (172)


#define LCD_DRAW_BUFF_HEIGHT (20)
#define LCD_DRAW_BUFF_DOUBLE (1)


static char *TAG = "lvgl_example";

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;


static esp_err_t app_lvgl_init(void);

// Two-phase PPQ control:
//   1. Long-press (>= 3 s) on either side reveals the current PPQ for 5 s
//      (no value change).
//   2. While the readout is visible, each subsequent press on the LEFT half
//      decreases PPQ by one step, RIGHT half increases. Each press extends
//      the visible window by another 5 s.
// Polled here rather than via LVGL events so we catch presses on any pixel.
#define LONG_PRESS_MS  3000
#define HALF_X         (LCD_H_RES / 2)

static void poll_ppq_input(void)
{
    static int64_t press_start_ms = 0;
    static int     press_side     = 0;   // -1 left, +1 right, 0 none
    static bool    handled        = false;  // already acted on this press

    if (!lvgl_touch_indev) return;

    // LVGL 8 has no public state getter; the struct field is exposed as "internal".
    lv_indev_state_t state = lvgl_touch_indev->proc.state;
    int64_t          now   = esp_timer_get_time() / 1000;
    bool             open  = lvgl_ui_is_ppq_visible();

    if (state == LV_INDEV_STATE_PRESSED) {
        lv_point_t pt;
        lv_indev_get_point(lvgl_touch_indev, &pt);
        int side = (pt.x < HALF_X) ? -1 : +1;

        // New press, or finger crossed the midline
        if (press_side != side) {
            press_side     = side;
            press_start_ms = now;
            handled        = false;
        }

        if (handled) return;

        if (open) {
            // Adjust window is open: each press steps PPQ once
            handled = true;
            if (lvgl_port_lock(0)) {
                midi_clock_ppq_step(side);
                lvgl_ui_refresh_ppq();
                lvgl_port_unlock();
            }
        } else if ((now - press_start_ms) >= LONG_PRESS_MS) {
            // Window closed: 3-s hold reveals the current value (no change)
            handled = true;
            if (lvgl_port_lock(0)) {
                lvgl_ui_refresh_ppq();
                lvgl_port_unlock();
            }
        }
    } else {
        press_side     = 0;
        press_start_ms = 0;
        handled        = false;
    }
}

static void idle_dim_task(void *arg)
{
    bool dimmed = false;

    for (;;) {
        poll_ppq_input();

        bool    set_bright = false;
        uint8_t new_bright = 100;
        if (lvgl_port_lock(0)) {
            uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
            if (inactive_ms > 20000 && !dimmed) {
                dimmed = true;
                lvgl_ui_set_dimmed(true);
                set_bright = true;
                new_bright = 10;
            } else if (inactive_ms <= 20000 && dimmed) {
                dimmed = false;
                lvgl_ui_set_dimmed(false);
                set_bright = true;
                new_bright = 100;
            }
            lvgl_port_unlock();
        }
        if (set_bright)
            bsp_display_set_brightness(new_bright);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void app_main(void)
{
    i2c_master_bus_handle_t i2c_bus_handle;
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    i2c_bus_handle = bsp_i2c_init();
    bsp_display_init(&io_handle, &panel_handle, LCD_H_RES * LCD_DRAW_BUFF_HEIGHT);
    bsp_touch_init(&touch_handle, i2c_bus_handle, LCD_H_RES, LCD_V_RES, DISPLAY_ROTATION);

    ESP_ERROR_CHECK(app_lvgl_init());

    bsp_display_brightness_init();
    bsp_display_set_brightness(100);

    midi_clock_init();
    usb_midi_init();

    if (lvgl_port_lock(0))
    {
        lvgl_ui_init();
        lvgl_port_unlock();
    }

    xTaskCreate(idle_dim_task, "idle_dim", 2048, NULL, 2, NULL);
}

static esp_err_t app_lvgl_init(void)
{
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,       /* LVGL task priority */
        .task_stack = 1024 * 10,  /* LVGL task stack size */
        .task_affinity = -1,      /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500, /* Maximum sleep in LVGL task */
        .timer_period_ms = 5      /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        }};
#if DISPLAY_ROTATION == 90
    disp_cfg.rotation.swap_xy = true;
    disp_cfg.rotation.mirror_x = true;
    disp_cfg.rotation.mirror_y = false;
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 34));
#elif DISPLAY_ROTATION == 180
    disp_cfg.rotation.swap_xy = false;
    disp_cfg.rotation.mirror_x = true;
    disp_cfg.rotation.mirror_y = true;
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 34, 0));
#elif DISPLAY_ROTATION == 270
    disp_cfg.rotation.swap_xy = true;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = true;
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 34));
#else
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 34, 0));
#endif
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

    return ESP_OK;
}

