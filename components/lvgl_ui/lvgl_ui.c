#include "lvgl_ui.h"
#include "esp_timer.h"
#include <string.h>
#include <stdbool.h>

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

// State
static uint16_t current_tempo = 120;
static bool is_playing = false;

// UI handles updated at runtime
static lv_obj_t *tempo_label;
static lv_obj_t *start_stop_btn;
static lv_obj_t *start_stop_label;

// Tap tempo ring buffer
#define TAP_MAX          8
#define TAP_TIMEOUT_US   3000000LL

static int64_t tap_times[TAP_MAX];
static int     tap_count = 0;

// ------------------------------------------------------------------ helpers

static void update_tempo_display(void)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", current_tempo);
    lv_label_set_text(tempo_label, buf);
}

static void update_start_stop_btn(void)
{
    if (is_playing) {
        lv_label_set_text(start_stop_label, LV_SYMBOL_STOP " STOP");
        lv_obj_set_style_bg_color(start_stop_btn,      COLOR_BTN_R,       LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(start_stop_btn, COLOR_BTN_R,       LV_PART_MAIN);
        lv_obj_set_style_border_color(start_stop_btn,  COLOR_RED,         LV_PART_MAIN);
        lv_obj_set_style_shadow_color(start_stop_btn,  COLOR_RED,         LV_PART_MAIN);
        lv_obj_set_style_text_color(start_stop_label,  COLOR_RED,         LV_PART_MAIN);
    } else {
        lv_label_set_text(start_stop_label, LV_SYMBOL_PLAY " START");
        lv_obj_set_style_bg_color(start_stop_btn,      COLOR_BTN_G,       LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(start_stop_btn, COLOR_BTN_G,       LV_PART_MAIN);
        lv_obj_set_style_border_color(start_stop_btn,  COLOR_GREEN,       LV_PART_MAIN);
        lv_obj_set_style_shadow_color(start_stop_btn,  COLOR_GREEN,       LV_PART_MAIN);
        lv_obj_set_style_text_color(start_stop_label,  COLOR_GREEN,       LV_PART_MAIN);
    }
}

// ------------------------------------------------------------------ callbacks

static void minus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    tap_count = 0;
    if (current_tempo > 20) { current_tempo--; update_tempo_display(); }
}

static void plus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    tap_count = 0;
    if (current_tempo < 300) { current_tempo++; update_tempo_display(); }
}

static void tap_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

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

static void start_stop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    is_playing = !is_playing;
    update_start_stop_btn();
}

// ------------------------------------------------------------------ style helpers

static void make_px_rect(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h, lv_color_t clr)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, h);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(r, clr, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(r, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(r, 0, LV_PART_MAIN);
}

static void make_glowline(lv_obj_t *parent, int y, lv_color_t color)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_pos(line, 0, y);
    lv_obj_set_size(line, lv_obj_get_width(parent), 2);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(line,      color,        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line,        LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line,  0,            LV_PART_MAIN);
    lv_obj_set_style_radius(line,        0,            LV_PART_MAIN);
    lv_obj_set_style_shadow_color(line,  color,        LV_PART_MAIN);
    lv_obj_set_style_shadow_width(line,  10,           LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(line,    LV_OPA_80,    LV_PART_MAIN);
}

static void apply_neon_btn(lv_obj_t *btn, lv_color_t border_clr, lv_color_t bg_clr)
{
    lv_obj_set_style_bg_color(btn,       bg_clr,           LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(btn,  bg_clr,           LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(btn,    LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn,         LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_set_style_border_color(btn,   border_clr,       LV_PART_MAIN);
    lv_obj_set_style_border_width(btn,   2,                LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn,     LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_set_style_radius(btn,         6,                LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn,   border_clr,       LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn,   14,               LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn,     LV_OPA_50,        LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn,        0,                LV_PART_MAIN);
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
    // the visible area is LVGL y=0..171 (172 rows) — the set_gap(0,34) call is
    // a hardware RAM addressing offset in the controller, not a display blank.
    // LVGL reports 320x240 so LV_ALIGN_BOTTOM_* anchors to y=240, putting
    // elements far off the physical screen.  A root container sized 320x172
    // gives a clean canvas; LV_ALIGN_BOTTOM_* inside it hits y=172 which is
    // exactly the physical bottom edge.
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 320, 172);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,    COLOR_BG,     LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root,      LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0,           LV_PART_MAIN);
    lv_obj_set_style_pad_all(root,     0,            LV_PART_MAIN);
    lv_obj_set_style_radius(root,      0,            LV_PART_MAIN);

    // Layout within root (320x172):
    //   y =  0..32   header bar
    //   y = 32..34   cyan glowline
    //   y = 34..138  content (BPM box centered here)
    //   y =138..140  pink glowline
    //   y =140..172  bottom controls

    make_glowline(root,  32, COLOR_CYAN);
    make_glowline(root, 138, COLOR_PINK);

    // ── Header ──────────────────────────────────────────────────────────────

    // "TICK" [tic-tac grid] "TAC" laid out as a flex row
    lv_obj_t *title_row = lv_obj_create(root);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(title_row, 10, 5);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(title_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_row, 5, LV_PART_MAIN);

    lv_obj_t *tick_lbl = lv_label_create(title_row);
    lv_label_set_text(tick_lbl, "TICK");
    lv_obj_set_style_text_font(tick_lbl,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(tick_lbl, COLOR_CYAN,              LV_PART_MAIN);

    // 14×14 tic-tac-toe grid: 2px lines at 1/3 and 2/3 of 14px → x/y = 4 and 8
    lv_obj_t *ttt_grid = lv_obj_create(title_row);
    lv_obj_remove_style_all(ttt_grid);
    lv_obj_set_size(ttt_grid, 14, 14);
    lv_obj_clear_flag(ttt_grid, LV_OBJ_FLAG_SCROLLABLE);
    make_px_rect(ttt_grid, 0, 4, 14, 2, COLOR_CYAN);
    make_px_rect(ttt_grid, 0, 8, 14, 2, COLOR_CYAN);
    //make_px_rect(ttt_grid, 4, 0, 2, 14, COLOR_CYAN);
    //make_px_rect(ttt_grid, 8, 0, 2, 14, COLOR_CYAN);

    lv_obj_t *tac_lbl = lv_label_create(title_row);
    lv_label_set_text(tac_lbl, "TAC");
    lv_obj_set_style_text_font(tac_lbl,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(tac_lbl, COLOR_CYAN,              LV_PART_MAIN);

    start_stop_btn = lv_btn_create(root);
    lv_obj_remove_style_all(start_stop_btn);
    lv_obj_set_size(start_stop_btn, 90, 26);
    lv_obj_align(start_stop_btn, LV_ALIGN_TOP_RIGHT, -8, 3);
    lv_obj_set_style_bg_grad_dir(start_stop_btn,  LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_border_width(start_stop_btn,  2,            LV_PART_MAIN);
    lv_obj_set_style_border_opa(start_stop_btn,    LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(start_stop_btn,        5,            LV_PART_MAIN);
    lv_obj_set_style_shadow_width(start_stop_btn,  12,           LV_PART_MAIN);
    lv_obj_set_style_pad_all(start_stop_btn,       0,            LV_PART_MAIN);
    lv_obj_set_style_shadow_width(start_stop_btn,  22,           LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(start_stop_btn,    LV_OPA_80,    LV_STATE_PRESSED);
    lv_obj_add_event_cb(start_stop_btn, start_stop_cb, LV_EVENT_CLICKED, NULL);

    start_stop_label = lv_label_create(start_stop_btn);
    lv_obj_set_style_text_font(start_stop_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(start_stop_label);
    update_start_stop_btn();

    // ── BPM box (tap-tempo target) ───────────────────────────────────────────

    lv_obj_t *bpm_box = lv_obj_create(root);
    lv_obj_set_size(bpm_box, 220, 88);
    lv_obj_align(bpm_box, LV_ALIGN_CENTER, 0, -5);
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
    lv_obj_add_event_cb(bpm_box, tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tap_hint = lv_label_create(bpm_box);
    lv_label_set_text(tap_hint, "TAP TEMPO");
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
    lv_obj_remove_style_all(minus_btn);           // clear theme BEFORE size/align
    lv_obj_set_size(minus_btn, 70, 30);
    lv_obj_align(minus_btn, LV_ALIGN_BOTTOM_LEFT, 10, -2);
    apply_neon_btn(minus_btn, COLOR_CYAN, COLOR_BTN_C);
    lv_obj_add_event_cb(minus_btn, minus_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *minus_lbl = lv_label_create(minus_btn);
    lv_label_set_text(minus_lbl, "-");
    lv_obj_set_style_text_font(minus_lbl,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(minus_lbl, COLOR_CYAN,             LV_PART_MAIN);
    lv_obj_center(minus_lbl);

    lv_obj_t *plus_btn = lv_btn_create(root);
    lv_obj_remove_style_all(plus_btn);            // clear theme BEFORE size/align
    lv_obj_set_size(plus_btn, 70, 30);
    lv_obj_align(plus_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -2);
    apply_neon_btn(plus_btn, COLOR_CYAN, COLOR_BTN_C);
    lv_obj_add_event_cb(plus_btn, plus_cb, LV_EVENT_CLICKED, NULL);

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
