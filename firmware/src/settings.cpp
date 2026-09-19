#include "settings.h"
#include "pomodoro.h"
#include "splash.h"
#include "theme.h"
#include "idle.h"
#include "hal/imu_hal.h"
#include "hal/board_caps.h"
#include <Arduino.h>

LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_16);

// Geometry tuned for the 480x480 panels; everything hangs off these so a
// smaller board only needs different numbers.
#define PAD_X      36
#define TITLE_Y    28
#define ROW_Y0     96
#define ROW_STEP   68
#define ROW_H      56
#define STEP_W     60      // the - and + buttons
#define VALUE_W    118
#define TOGGLE_W   120

#define FOCUS_STEP_MIN  5
#define BREAK_STEP_MIN  1

static lv_obj_t* root       = nullptr;
static lv_obj_t* toggle_btn = nullptr;
static lv_obj_t* toggle_lbl = nullptr;
static lv_obj_t* focus_val  = nullptr;
static lv_obj_t* break_val  = nullptr;
static lv_obj_t* side_hint  = nullptr;
static bool      s_open     = false;
static int8_t    shown_quad = -1;
static int8_t    shown_fq   = -1;

enum { FIELD_FOCUS = 0, FIELD_BREAK = 1 };

static lv_obj_t* make_button(lv_obj_t* parent, int x, int y, int w, int h,
                             const char* text, const lv_font_t* font,
                             lv_obj_t** out_label) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, THEME_BAR_BG, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, THEME_TEXT, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (out_label) *out_label = l;
    return b;
}

static lv_obj_t* make_row_label(int y, const char* text) {
    lv_obj_t* l = lv_label_create(root);
    lv_obj_set_style_text_font(l, &font_styrene_28, 0);
    lv_obj_set_style_text_color(l, THEME_TEXT, 0);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, PAD_X, y + (ROW_H - 28) / 2 - 2);
    return l;
}

static void refresh(void) {
    const PomodoroConfig& c = pomodoro_config();

    lv_label_set_text(toggle_lbl, c.enabled ? "ON" : "OFF");
    lv_obj_set_style_bg_color(toggle_btn, c.enabled ? THEME_ACCENT : THEME_BAR_BG, 0);
    lv_obj_set_style_text_color(toggle_lbl, c.enabled ? THEME_TEXT : THEME_DIM, 0);

    lv_label_set_text_fmt(focus_val, "%u min", c.focus_min);
    lv_label_set_text_fmt(break_val, "%u min", c.break_min);

    shown_quad = -1;    // force the side hint to repaint
}

static void toggle_cb(lv_event_t* e) {
    (void)e;
    PomodoroConfig c = pomodoro_config();
    c.enabled = !c.enabled;
    pomodoro_set_config(c);
    refresh();
}

// user_data packs the field in bit 1 and the direction in bit 0.
static void step_cb(lv_event_t* e) {
    const uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    const int  field = (int)(tag >> 1);
    const int  dir   = (tag & 1) ? +1 : -1;

    PomodoroConfig c = pomodoro_config();
    if (field == FIELD_FOCUS) {
        const int v = c.focus_min + dir * FOCUS_STEP_MIN;
        c.focus_min = pomodoro_clamp_focus(v);
    } else {
        const int v = c.break_min + dir * BREAK_STEP_MIN;
        c.break_min = pomodoro_clamp_break(v);
    }
    pomodoro_set_config(c);
    refresh();
}

static void side_cb(lv_event_t* e) {
    (void)e;
    PomodoroConfig c = pomodoro_config();
    c.focus_quad = imu_hal_rotation_quadrant() & 3;
    pomodoro_set_config(c);
    refresh();
}

static void done_cb(lv_event_t* e) {
    (void)e;
    settings_close();
}

static void make_stepper(int y, int field, lv_obj_t** out_value) {
    const int right = board_caps().width - PAD_X;
    const int plus_x  = right - STEP_W;
    const int value_x = plus_x - VALUE_W;
    const int minus_x = value_x - STEP_W;

    lv_obj_t* minus = make_button(root, minus_x, y, STEP_W, ROW_H, "-", &font_styrene_28, nullptr);
    lv_obj_t* plus  = make_button(root, plus_x,  y, STEP_W, ROW_H, "+", &font_styrene_28, nullptr);
    // Step on the press itself, then keep stepping while held.
    const lv_event_code_t codes[] = { LV_EVENT_PRESSED, LV_EVENT_LONG_PRESSED_REPEAT };
    for (lv_event_code_t code : codes) {
        lv_obj_add_event_cb(minus, step_cb, code, (void*)(uintptr_t)((field << 1) | 0));
        lv_obj_add_event_cb(plus,  step_cb, code, (void*)(uintptr_t)((field << 1) | 1));
    }

    lv_obj_t* v = lv_label_create(root);
    lv_obj_set_width(v, VALUE_W);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(v, &font_styrene_24, 0);
    lv_obj_set_style_text_color(v, THEME_TEXT, 0);
    lv_obj_set_pos(v, value_x, y + (ROW_H - 24) / 2 - 2);
    *out_value = v;
}

void settings_init(lv_obj_t* parent) {
    const BoardCaps& c = board_caps();
    // Only the Pomodoro lives here, and it needs the IMU.
    if (!c.has_imu) return;

    root = lv_obj_create(parent);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, c.width, c.height);
    lv_obj_set_style_bg_color(root, THEME_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(root);
    lv_obj_set_style_text_font(title, &font_tiempos_34, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT, 0);
    lv_label_set_text(title, "Pomodoro");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, TITLE_Y);

    const int content_w = c.width - 2 * PAD_X;
    int y = ROW_Y0;

    make_row_label(y, "Enabled");
    toggle_btn = make_button(root, c.width - PAD_X - TOGGLE_W, y, TOGGLE_W, ROW_H,
                             "ON", &font_styrene_24, &toggle_lbl);
    lv_obj_add_event_cb(toggle_btn, toggle_cb, LV_EVENT_CLICKED, NULL);
    y += ROW_STEP;

    make_row_label(y, "Focus");
    make_stepper(y, FIELD_FOCUS, &focus_val);
    y += ROW_STEP;

    make_row_label(y, "Break");
    make_stepper(y, FIELD_BREAK, &break_val);
    y += ROW_STEP + 8;

    lv_obj_t* side = make_button(root, PAD_X, y, content_w, ROW_H,
                                 "Use this side for focus", &font_styrene_24, nullptr);
    lv_obj_add_event_cb(side, side_cb, LV_EVENT_CLICKED, NULL);
    y += ROW_H + 12;

    side_hint = lv_label_create(root);
    lv_obj_set_width(side_hint, content_w);
    lv_obj_set_style_text_align(side_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(side_hint, &font_styrene_16, 0);
    lv_obj_set_style_text_color(side_hint, THEME_DIM, 0);
    lv_label_set_text(side_hint, "");
    lv_obj_set_pos(side_hint, PAD_X, y);

    lv_obj_t* done_lbl = nullptr;
    lv_obj_t* done = make_button(root, (c.width - 200) / 2, c.height - 24 - ROW_H, 200, ROW_H,
                                 "Done", &font_styrene_24, &done_lbl);
    lv_obj_set_style_bg_color(done, THEME_ACCENT, 0);
    lv_obj_add_event_cb(done, done_cb, LV_EVENT_CLICKED, NULL);
}

void settings_tick(void) {
    if (!s_open) return;

    // Tell the user what the side they are holding it on does right now, so
    // setting the focus side is something you can see work.
    const int8_t q  = (int8_t)(imu_hal_rotation_quadrant() & 3);
    const int8_t fq = (int8_t)pomodoro_config().focus_quad;
    if (q == shown_quad && fq == shown_fq) return;
    shown_quad = q;
    shown_fq   = fq;

    if      (q == fq)             lv_label_set_text(side_hint, "This side: focus");
    else if (q == ((fq + 2) & 3)) lv_label_set_text(side_hint, "This side: break");
    else                          lv_label_set_text(side_hint, "This side: normal screens");
}

void settings_open(void) {
    if (!root || s_open) return;
    pomodoro_set_suspended(true);
    refresh();
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    s_open = true;
    settings_tick();
    idle_note_activity();
}

void settings_close(void) {
    if (!s_open) return;
    pomodoro_save_config();
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
    pomodoro_set_suspended(false);
    splash_request_full_redraw();
    idle_note_activity();
}

bool settings_is_open(void) { return s_open; }
