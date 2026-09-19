#include "pomodoro.h"
#include "splash.h"
#include "theme.h"
#include "idle.h"
#include "hal/imu_hal.h"
#include "hal/sound_hal.h"
#include "hal/board_caps.h"
#include <Arduino.h>

LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_16);

#define WORK_MS   (25UL * 60UL * 1000UL)
#define BREAK_MS  (5UL  * 60UL * 1000UL)

// Quadrants from imu_hal_rotation_quadrant(): 0 is the default mounting, 1 and
// 3 are the two sides, 2 is upside down. Only the sides run a block; both
// upright positions pause, so setting the device down the "wrong" way up does
// not silently start counting.
#define QUAD_WORK   1
#define QUAD_BREAK  3

#define ARC_INSET   56     // ring margin from the screen edge
#define ARC_WIDTH   16

enum { MODE_WORK = 0, MODE_BREAK = 1, MODE_COUNT };

static const uint32_t full_ms[MODE_COUNT] = { WORK_MS, BREAK_MS };

static lv_obj_t* root     = nullptr;
static lv_obj_t* arc      = nullptr;
static lv_obj_t* time_lbl = nullptr;
static lv_obj_t* mode_lbl = nullptr;
static lv_obj_t* hint_lbl = nullptr;

static bool     s_active = false;
static int      cur_mode = -1;                 // -1 while paused/hidden
static uint32_t remaining[MODE_COUNT] = { WORK_MS, BREAK_MS };
static bool     done[MODE_COUNT]      = { false, false };
static uint32_t last_ms   = 0;
static int32_t  shown_sec = -1;                // last value painted, in seconds

static lv_color_t mode_color(int mode) {
    return (mode == MODE_BREAK) ? THEME_GREEN : THEME_ACCENT;
}

static void paint(int mode) {
    const uint32_t left = remaining[mode];
    const int32_t  secs = (int32_t)((left + 999) / 1000);   // round up: 25:00 on start
    if (secs == shown_sec) return;
    shown_sec = secs;

    lv_label_set_text_fmt(time_lbl, "%02d:%02d", (int)(secs / 60), (int)(secs % 60));

    // The ring empties as the block runs.
    const uint32_t total = full_ms[mode];
    lv_arc_set_value(arc, (int32_t)((uint64_t)left * 1000 / total));

    if (done[mode]) {
        lv_label_set_text(mode_lbl, "DONE");
        lv_label_set_text(hint_lbl, "Stand it up");
    } else {
        lv_label_set_text(mode_lbl, mode == MODE_BREAK ? "BREAK" : "FOCUS");
        lv_label_set_text(hint_lbl, "Tap to restart");
    }
}

static void enter(int mode) {
    // A block that ran out last time starts over rather than sitting at 00:00.
    if (done[mode]) {
        done[mode]      = false;
        remaining[mode] = full_ms[mode];
    }
    cur_mode  = mode;
    last_ms   = millis();
    shown_sec = -1;

    const lv_color_t col = mode_color(mode);
    lv_obj_set_style_arc_color(arc, col, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(mode_lbl, col, 0);

    paint(mode);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    s_active = true;
    idle_note_activity();
}

static void leave(void) {
    cur_mode = -1;
    if (!s_active) return;
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    s_active = false;
    // Same reason as the charge overlay: the splash only repaints the cells
    // that changed, so what this covered would stay black until the creature
    // happened to move through it.
    splash_request_full_redraw();
    idle_note_activity();
}

static void tap_cb(lv_event_t* e) {
    (void)e;
    pomodoro_restart();
}

void pomodoro_restart(void) {
    if (cur_mode < 0) return;
    done[cur_mode]      = false;
    remaining[cur_mode] = full_ms[cur_mode];
    last_ms             = millis();
    shown_sec           = -1;
    paint(cur_mode);
    idle_note_activity();
}

void pomodoro_init(lv_obj_t* parent) {
    const BoardCaps& c = board_caps();
    if (!c.has_imu) return;

    const int16_t side = (c.width < c.height ? c.width : c.height) - 2 * ARC_INSET;

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
    lv_obj_add_event_cb(root, tap_cb, LV_EVENT_CLICKED, NULL);

    arc = lv_arc_create(root);
    lv_obj_set_size(arc, side, side);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);          // start at twelve o'clock
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 1000);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, THEME_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, THEME_ACCENT, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    // The ring is decoration; the tap belongs to the overlay underneath it.
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    mode_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(mode_lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(mode_lbl, THEME_ACCENT, 0);
    lv_label_set_text(mode_lbl, "FOCUS");
    lv_obj_align(mode_lbl, LV_ALIGN_CENTER, 0, -90);

    time_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(time_lbl, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(time_lbl, THEME_TEXT, 0);
    lv_label_set_text(time_lbl, "25:00");
    lv_obj_center(time_lbl);

    hint_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(hint_lbl, &font_styrene_16, 0);
    lv_obj_set_style_text_color(hint_lbl, THEME_DIM, 0);
    lv_label_set_text(hint_lbl, "Tap to restart");
    lv_obj_align(hint_lbl, LV_ALIGN_CENTER, 0, 96);
}

void pomodoro_tick(void) {
    if (!root) return;

    const uint8_t q = imu_hal_rotation_quadrant();
    const int want = (q == QUAD_WORK)  ? MODE_WORK
                   : (q == QUAD_BREAK) ? MODE_BREAK
                                       : -1;

    if (want != cur_mode) {
        if (want < 0) leave();
        else          enter(want);
        return;                     // the transition already painted
    }
    if (cur_mode < 0) return;

    const uint32_t now     = millis();
    const uint32_t elapsed = now - last_ms;
    last_ms = now;

    if (!done[cur_mode]) {
        if (elapsed >= remaining[cur_mode]) {
            remaining[cur_mode] = 0;
            done[cur_mode]      = true;
            Serial.printf("Pomodoro: %s block finished\n",
                          cur_mode == MODE_BREAK ? "break" : "focus");
            sound_hal_play_reset();     // no-op on boards without a buzzer
        } else {
            remaining[cur_mode] -= elapsed;
        }
        // A block in progress keeps the panel awake; the idle timer only ever
        // sees touches and buttons otherwise.
        idle_note_activity();
    }

    paint(cur_mode);
}

bool pomodoro_is_active(void) { return s_active; }
