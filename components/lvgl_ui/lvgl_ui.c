#include "lvgl_ui.h"
#include "midi_clock.h"
#include "esp_timer.h"
#include <string.h>
#include <stdbool.h>

#include "esp_lvgl_port.h"

static void start_beat_pulse(void);
static void stop_beat_pulse(void);

// Comfortaa Bold 20px — used only for the TICK / TAC logo text
LV_FONT_DECLARE(comfortaa);



// Vaporwave palette
#define COLOR_BG       lv_color_make(10,  0,  25)
#define COLOR_PANEL    lv_color_make(20,  5,  45)
#define COLOR_PINK     lv_color_make(255, 50, 200)
#define COLOR_CYAN     lv_color_make(0,  220, 255)
#define COLOR_TEXT     lv_color_make(240, 240, 255)
#define COLOR_DIM      lv_color_make(120, 100, 160)
#define COLOR_GREEN    lv_color_make(0,  255, 150)
#define COLOR_RED      lv_color_make(255,  60, 100)
#define COLOR_BTN_C    lv_color_make(0,   20,  35)
#define COLOR_BTN_G    lv_color_make(0,   30,  20)
#define COLOR_BTN_R    lv_color_make(40,   0,  15)

// Tempo / play state
static uint16_t current_tempo = 120;
static bool     is_playing    = false;

// UI handles updated at runtime
static lv_obj_t *tempo_label;
static lv_obj_t *start_stop_btn;
static lv_obj_t *start_stop_label;
static lv_obj_t *bpm_box_obj;

// Tap tempo ring buffer
#define TAP_MAX        8
#define TAP_TIMEOUT_US 3000000LL

static int64_t tap_times[TAP_MAX];
static int     tap_count = 0;

// Press-and-hold state for +/- buttons
static bool     holding_minus    = false;
static bool     holding_plus     = false;
static uint32_t hold_count_minus = 0;
static uint32_t hold_count_plus  = 0;

// Swipe state on BPM box
static lv_coord_t swipe_start_y  = 0;
static uint16_t   swipe_base_bpm = 0;
static bool       swipe_active   = false;

// ------------------------------------------------------------------ helpers

static void update_tempo_display(void)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", current_tempo);
    lv_label_set_text(tempo_label, buf);
    midi_clock_set_tempo(current_tempo);
    if (is_playing) {
        stop_beat_pulse();
        start_beat_pulse();
    }
}

static void update_start_stop_btn(void)
{
    if (is_playing) {
        lv_label_set_text(start_stop_label, LV_SYMBOL_STOP " STOP");
        lv_obj_set_style_bg_color(start_stop_btn,      COLOR_BTN_R, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(start_stop_btn, COLOR_BTN_R, LV_PART_MAIN);
        lv_obj_set_style_border_color(start_stop_btn,  COLOR_RED,   LV_PART_MAIN);
        lv_obj_set_style_shadow_color(start_stop_btn,  COLOR_RED,   LV_PART_MAIN);
        lv_obj_set_style_text_color(start_stop_label,  COLOR_RED,   LV_PART_MAIN);
    } else {
        lv_label_set_text(start_stop_label, LV_SYMBOL_PLAY " START");
        lv_obj_set_style_bg_color(start_stop_btn,      COLOR_BTN_G,   LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(start_stop_btn, COLOR_BTN_G,   LV_PART_MAIN);
        lv_obj_set_style_border_color(start_stop_btn,  COLOR_GREEN,   LV_PART_MAIN);
        lv_obj_set_style_shadow_color(start_stop_btn,  COLOR_GREEN,   LV_PART_MAIN);
        lv_obj_set_style_text_color(start_stop_label,  COLOR_GREEN,   LV_PART_MAIN);
    }
}

// ------------------------------------------------------------------ callbacks

static void minus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        holding_minus    = false;
        hold_count_minus = 0;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        holding_minus = true;
        tap_count = 0;
        if (current_tempo > 20) { current_tempo--; update_tempo_display(); }
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        hold_count_minus++;
        uint16_t step = 1 + hold_count_minus / 15;
        if (step > 10) step = 10;
        tap_count = 0;
        current_tempo = (current_tempo > 20 + step) ? current_tempo - step : 20;
        update_tempo_display();
    } else if (code == LV_EVENT_CLICKED) {
        if (!holding_minus) {
            tap_count = 0;
            if (current_tempo > 20) { current_tempo--; update_tempo_display(); }
        }
        holding_minus = false;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        holding_minus = false;
    }
}

static void plus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        holding_plus    = false;
        hold_count_plus = 0;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        holding_plus = true;
        tap_count = 0;
        if (current_tempo < 300) { current_tempo++; update_tempo_display(); }
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        hold_count_plus++;
        uint16_t step = 1 + hold_count_plus / 15;
        if (step > 10) step = 10;
        tap_count = 0;
        current_tempo = (current_tempo < 300 - step) ? current_tempo + step : 300;
        update_tempo_display();
    } else if (code == LV_EVENT_CLICKED) {
        if (!holding_plus) {
            tap_count = 0;
            if (current_tempo < 300) { current_tempo++; update_tempo_display(); }
        }
        holding_plus = false;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        holding_plus = false;
    }
}

static void bpm_box_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        swipe_start_y  = pt.y;
        swipe_base_bpm = current_tempo;
        swipe_active   = false;
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        lv_coord_t dy = swipe_start_y - pt.y; // positive = dragged up = increase BPM
        if (LV_ABS(dy) > 8) {
            swipe_active = true;
            int32_t new_bpm = (int32_t)swipe_base_bpm + (int32_t)(dy / 3);
            if (new_bpm < 20)  new_bpm = 20;
            if (new_bpm > 300) new_bpm = 300;
            if ((uint16_t)new_bpm != current_tempo) {
                tap_count     = 0;
                current_tempo = (uint16_t)new_bpm;
                update_tempo_display();
            }
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        swipe_active = false;
    } else if (code == LV_EVENT_CLICKED) {
        if (swipe_active) return;
        // Tap tempo
        int64_t now = esp_timer_get_time();
        if (tap_count > 0 && (now - tap_times[tap_count - 1]) > TAP_TIMEOUT_US) {
            tap_count = 0;
        }
        if (tap_count < TAP_MAX) {
            tap_times[tap_count++] = now;
        } else {
            memmove(tap_times, tap_times + 1, (TAP_MAX - 1) * sizeof(int64_t));
            tap_times[TAP_MAX - 1] = now;
        }
        if (tap_count >= 2) {
            int64_t avg_us = (tap_times[tap_count - 1] - tap_times[0]) / (tap_count - 1);
            if (avg_us > 0) {
                uint16_t bpm = (uint16_t)(60000000LL / avg_us);
                if (bpm < 20)  bpm = 20;
                if (bpm > 300) bpm = 300;
                current_tempo = bpm;
                update_tempo_display();
            }
        }
    }
}

static void pulse_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_shadow_width((lv_obj_t *)obj, v, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa((lv_obj_t *)obj,
        (lv_opa_t)(LV_OPA_50 + (v - 22) * (LV_OPA_90 - LV_OPA_50) / (55 - 22)),
        LV_PART_MAIN);
}

static void start_beat_pulse(void)
{
    uint32_t beat_ms = 60000u / current_tempo;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bpm_box_obj);
    lv_anim_set_exec_cb(&a, pulse_anim_cb);
    lv_anim_set_values(&a, 22, 55);
    lv_anim_set_time(&a, beat_ms / 5);
    lv_anim_set_playback_time(&a, beat_ms * 4 / 5);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void stop_beat_pulse(void)
{
    lv_anim_del(bpm_box_obj, pulse_anim_cb);
    lv_obj_set_style_shadow_width(bpm_box_obj, 22, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(bpm_box_obj, LV_OPA_50, LV_PART_MAIN);
}

static void start_stop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    is_playing = !is_playing;
    update_start_stop_btn();
    if (is_playing) {
        midi_clock_start();
        start_beat_pulse();
    } else {
        midi_clock_stop();
        stop_beat_pulse();
    }
}

// ------------------------------------------------------------------ style helpers


static void make_glowline(lv_obj_t *parent, int y, lv_color_t color)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_pos(line, 0, y);
    lv_obj_set_size(line, lv_obj_get_width(parent), 2);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(line,     color,        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line,       LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0,            LV_PART_MAIN);
    lv_obj_set_style_radius(line,       0,            LV_PART_MAIN);
    lv_obj_set_style_shadow_color(line, color,        LV_PART_MAIN);
    lv_obj_set_style_shadow_width(line, 10,           LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(line,   LV_OPA_80,    LV_PART_MAIN);
}

static void apply_neon_btn(lv_obj_t *btn, lv_color_t border_clr, lv_color_t bg_clr)
{
    lv_obj_set_style_bg_color(btn,      bg_clr,           LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(btn, bg_clr,           LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(btn,   LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn,        LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_set_style_border_color(btn,  border_clr,       LV_PART_MAIN);
    lv_obj_set_style_border_width(btn,  2,                LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn,    LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_set_style_radius(btn,        6,                LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn,  border_clr,       LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn,  14,               LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn,    LV_OPA_50,        LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn,       0,                LV_PART_MAIN);
    // Pressed: flood with accent color and boost glow
    lv_obj_set_style_bg_color(btn,      border_clr,       LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, border_clr,       LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(btn,   LV_GRAD_DIR_NONE, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn,  24,               LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(btn,    LV_OPA_80,        LV_STATE_PRESSED);
}

// ------------------------------------------------------------------ init

void lvgl_ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COLOR_BG,     LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr,   LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr,  0,            LV_PART_MAIN);

    // The JD9853 panel is 172x320 physical pixels.  In landscape (rotation 90)
    // the visible area is LVGL y=0..171 — the set_gap(0,34) is a hardware RAM
    // addressing offset, not a display blank.  Root container sized 320x172
    // ensures LV_ALIGN_BOTTOM_* hits the physical bottom edge.
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 320, 172);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,     COLOR_BG,     LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root,       LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0,            LV_PART_MAIN);
    lv_obj_set_style_pad_all(root,      0,            LV_PART_MAIN);
    lv_obj_set_style_radius(root,       0,            LV_PART_MAIN);

    // Layout within root (320x172):
    //   y =  0..32   header bar
    //   y = 32..34   cyan glowline
    //   y = 34..138  content (BPM box centred here)
    //   y =138..140  pink glowline
    //   y =140..172  bottom controls

    make_glowline(root,  32, COLOR_CYAN);
    make_glowline(root, 138, COLOR_PINK);

    // ── Header ──────────────────────────────────────────────────────────────

    // "TICK" [tic-tac] "TAC" — Comfortaa Bold, flex row
    lv_obj_t *title_row = lv_obj_create(root);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(title_row, 14, 10);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(title_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_row, 5, LV_PART_MAIN);

    lv_obj_t *tick_lbl = lv_label_create(title_row);
    lv_label_set_text(tick_lbl, "tick");
    lv_obj_set_style_text_font(tick_lbl,  &comfortaa, LV_PART_MAIN);
    lv_obj_set_style_text_color(tick_lbl, COLOR_CYAN,         LV_PART_MAIN);

    //capsule dash
    lv_obj_t *tt_grid = lv_obj_create(title_row);
    lv_obj_remove_style_all(tt_grid);
    lv_obj_set_size(tt_grid, 14, 6);   // make it wide + short (important for pill look)
    lv_obj_clear_flag(tt_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(tt_grid, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(tt_grid, COLOR_CYAN, 0);
    lv_obj_set_style_bg_opa(tt_grid, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(tick_lbl,  &comfortaa, LV_PART_MAIN);
    
    lv_obj_t *tac_lbl = lv_label_create(title_row);
    lv_label_set_text(tac_lbl, "tac");
    lv_obj_set_style_text_font(tac_lbl,  &comfortaa, LV_PART_MAIN);
    lv_obj_set_style_text_color(tac_lbl, COLOR_CYAN,         LV_PART_MAIN);

    start_stop_btn = lv_btn_create(root);
    lv_obj_remove_style_all(start_stop_btn);
    lv_obj_set_size(start_stop_btn, 90, 30);
    lv_obj_align(start_stop_btn, LV_ALIGN_TOP_RIGHT, -10, 2);
    lv_obj_set_style_bg_grad_dir(start_stop_btn,  LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_border_width(start_stop_btn, 2,            LV_PART_MAIN);
    lv_obj_set_style_border_opa(start_stop_btn,   LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(start_stop_btn,       5,            LV_PART_MAIN);
    lv_obj_set_style_shadow_width(start_stop_btn, 12,           LV_PART_MAIN);
    lv_obj_set_style_pad_all(start_stop_btn,      0,            LV_PART_MAIN);
    lv_obj_set_style_shadow_width(start_stop_btn, 22,           LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(start_stop_btn,   LV_OPA_80,    LV_STATE_PRESSED);
    lv_obj_add_event_cb(start_stop_btn, start_stop_cb, LV_EVENT_CLICKED, NULL);

    start_stop_label = lv_label_create(start_stop_btn);
    lv_obj_set_style_text_font(start_stop_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_center(start_stop_label);
    update_start_stop_btn();

    // ── BPM box (tap tempo + swipe to set) ──────────────────────────────────

    bpm_box_obj = lv_obj_create(root);
    lv_obj_t *bpm_box = bpm_box_obj;
    lv_obj_set_size(bpm_box, 220, 88);
    lv_obj_align(bpm_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(bpm_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bpm_box,   LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(bpm_box,     COLOR_PANEL,  LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bpm_box,       LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bpm_box, COLOR_PINK,   LV_PART_MAIN);
    lv_obj_set_style_border_width(bpm_box, 2,            LV_PART_MAIN);
    lv_obj_set_style_border_opa(bpm_box,   LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bpm_box,       8,            LV_PART_MAIN);
    lv_obj_set_style_shadow_color(bpm_box, COLOR_PINK,   LV_PART_MAIN);
    lv_obj_set_style_shadow_width(bpm_box, 22,           LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(bpm_box,   LV_OPA_50,    LV_PART_MAIN);
    lv_obj_set_style_pad_all(bpm_box,      0,            LV_PART_MAIN);
    lv_obj_set_style_bg_color(bpm_box,     lv_color_make(50, 0, 90), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(bpm_box, 36,                        LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(bpm_box,   LV_OPA_80,                 LV_STATE_PRESSED);
    lv_obj_add_event_cb(bpm_box, bpm_box_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *tap_hint = lv_label_create(bpm_box);
    lv_label_set_text(tap_hint, "TAP  /  SWIPE");
    lv_obj_set_style_text_font(tap_hint,  &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(tap_hint, COLOR_DIM,              LV_PART_MAIN);
    lv_obj_align(tap_hint, LV_ALIGN_TOP_MID, 0, 4);

    tempo_label = lv_label_create(bpm_box);
    lv_label_set_text(tempo_label, "120");
    lv_obj_set_style_text_font(tempo_label,  &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(tempo_label, COLOR_TEXT,             LV_PART_MAIN);
    lv_obj_align(tempo_label, LV_ALIGN_CENTER, 0, 4);

    lv_obj_t *bpm_lbl = lv_label_create(bpm_box);
    lv_label_set_text(bpm_lbl, "BPM");
    lv_obj_set_style_text_font(bpm_lbl,  &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(bpm_lbl, COLOR_PINK,             LV_PART_MAIN);
    lv_obj_align(bpm_lbl, LV_ALIGN_BOTTOM_MID, 0, -4);

    // ── Bottom controls ──────────────────────────────────────────────────────

    lv_obj_t *minus_btn = lv_btn_create(root);
    lv_obj_remove_style_all(minus_btn);
    lv_obj_set_size(minus_btn, 70, 30);
    lv_obj_align(minus_btn, LV_ALIGN_BOTTOM_LEFT, 10, -2);
    apply_neon_btn(minus_btn, COLOR_CYAN, COLOR_BTN_C);
    lv_obj_add_event_cb(minus_btn, minus_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *minus_lbl = lv_label_create(minus_btn);
    lv_label_set_text(minus_lbl, "-");
    lv_obj_set_style_text_font(minus_lbl,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(minus_lbl, COLOR_CYAN,             LV_PART_MAIN);
    lv_obj_center(minus_lbl);

    lv_obj_t *plus_btn = lv_btn_create(root);
    lv_obj_remove_style_all(plus_btn);
    lv_obj_set_size(plus_btn, 70, 30);
    lv_obj_align(plus_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -2);
    apply_neon_btn(plus_btn, COLOR_CYAN, COLOR_BTN_C);
    lv_obj_add_event_cb(plus_btn, plus_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *plus_lbl = lv_label_create(plus_btn);
    lv_label_set_text(plus_lbl, "+");
    lv_obj_set_style_text_font(plus_lbl,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(plus_lbl, COLOR_CYAN,             LV_PART_MAIN);
    lv_obj_center(plus_lbl);

}

// ------------------------------------------------------------------ public API

uint16_t lvgl_ui_get_tempo(void)
{
    return current_tempo;
}

void lvgl_ui_set_tempo(uint16_t tempo)
{
    if (tempo >= 20 && tempo <= 300) {
        current_tempo = tempo;
        update_tempo_display();
    }
}

bool lvgl_ui_is_playing(void)
{
    return is_playing;
}

void lvgl_ui_set_dimmed(bool dimmed)
{
    if (dimmed) {
        stop_beat_pulse();
    } else if (is_playing) {
        start_beat_pulse();
    }
}
