#include "pomodoro.h"
#include "splash.h"
#include "settings.h"
#include "theme.h"
#include "idle.h"
#include "hal/imu_hal.h"
#include "hal/sound_hal.h"
#include "hal/board_caps.h"
#include <Arduino.h>
#include <Preferences.h>

LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_16);

// Quadrants from imu_hal_rotation_quadrant(). On the C6 AMOLED-2.16, 3 is
// upright on the desk and 1 upside down, standing on the button edge; 0 and 2
// are the two sides. Focus defaults to one of the sides with the break on the
// other, so upright — and upside down — keep the normal screens.
#define DEFAULT_FOCUS_QUAD  2
#define DEFAULT_FOCUS_MIN   25
#define DEFAULT_BREAK_MIN   5
#define DEFAULT_LONG_MIN    15

// A cycle nobody has touched for this long starts over: the dots are about
// one sitting, not the whole week.
#define CYCLE_STALE_MS      (3UL * 60UL * 60UL * 1000UL)

#define ARC_INSET   56     // ring margin from the screen edge
#define ARC_WIDTH   16
#define CLAWD_PX    110    // box the little Clawd is fitted into
#define DOT_SIZE    12
#define DOT_GAP     14
#define DOT_EMPTY   lv_color_hex(0x4a4946)   // THEME_BAR_BG vanishes on true black

// Clawd keeps you company: at the laptop while you focus, off on a cloud
// while you rest.
#define FOCUS_ANIM  "laptop"
#define BREAK_ANIM  "cloud"

enum { MODE_WORK = 0, MODE_BREAK = 1 };

static PomodoroConfig cfg = {
    true, DEFAULT_FOCUS_MIN, DEFAULT_BREAK_MIN, DEFAULT_FOCUS_QUAD, DEFAULT_LONG_MIN
};

static lv_obj_t* root     = nullptr;
static lv_obj_t* arc      = nullptr;
static lv_obj_t* clawd    = nullptr;
static lv_obj_t* time_lbl = nullptr;
static lv_obj_t* mode_lbl = nullptr;
static lv_obj_t* hint_lbl = nullptr;
static lv_obj_t* dots[POMODORO_CYCLE];

static bool     s_active  = false;
static bool     suspended = false;
static bool     claude_waiting = false;
static bool     codex_waiting  = false;
static int      cur_mode  = -1;       // -1 while hidden
static bool     long_break = false;   // the break on screen is the long one
static uint32_t total_ms  = 0;        // length of the block on screen
static uint32_t remaining = 0;
static bool     done      = false;
static uint32_t last_ms   = 0;
static int32_t  shown_sec = -1;       // last value painted, in seconds

static uint8_t  completed = 0;        // focus blocks finished in this cycle
static uint32_t last_completed_ms = 0;

static lv_color_t mode_color(void) {
    return (cur_mode == MODE_BREAK) ? THEME_GREEN : THEME_ACCENT;
}

// ---- Ring pulse when a block is over -------------------------------------------

static void pulse_exec(void* obj, int32_t v) {
    lv_obj_set_style_arc_opa((lv_obj_t*)obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void pulse_start(void) {
    lv_obj_set_style_arc_color(arc, mode_color(), LV_PART_MAIN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, pulse_exec);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void pulse_stop(void) {
    lv_anim_delete(arc, pulse_exec);
    lv_obj_set_style_arc_color(arc, THEME_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
}

// ---- Painting ------------------------------------------------------------------

static void paint_dots(void) {
    for (int i = 0; i < POMODORO_CYCLE; i++) {
        const bool filled = i < completed;
        lv_obj_set_style_bg_color(dots[i], filled ? THEME_ACCENT : DOT_EMPTY, 0);
    }
}

static const char* hint_text(void) {
    if (claude_waiting) return "Claude needs you";
    if (codex_waiting)  return "Codex needs you";
    if (!done)          return "Tap to restart";
    if (cur_mode == MODE_WORK) {
        return completed >= POMODORO_CYCLE ? "Turn it for a long break" : "Turn it for a break";
    }
    return long_break ? "Cycle done - turn it to focus" : "Turn it back to focus";
}

static void paint(bool force) {
    const int32_t secs = (int32_t)((remaining + 999) / 1000);   // round up: 25:00 on start
    if (secs == shown_sec && !force) return;
    shown_sec = secs;

    lv_label_set_text_fmt(time_lbl, "%02d:%02d", (int)(secs / 60), (int)(secs % 60));

    // The ring empties as the block runs.
    lv_arc_set_value(arc, total_ms ? (int32_t)((uint64_t)remaining * 1000 / total_ms) : 0);

    if (done)                       lv_label_set_text(mode_lbl, "DONE");
    else if (cur_mode == MODE_WORK) lv_label_set_text(mode_lbl, "FOCUS");
    else                            lv_label_set_text(mode_lbl, long_break ? "LONG BREAK" : "BREAK");

    lv_label_set_text(hint_lbl, hint_text());
    lv_obj_set_style_text_color(hint_lbl, (claude_waiting || codex_waiting) ? THEME_ACCENT : THEME_DIM, 0);
    paint_dots();
}

// ---- Blocks --------------------------------------------------------------------

static void start_block(void) {
    if (cur_mode == MODE_WORK) {
        total_ms = (uint32_t)cfg.focus_min * 60000UL;
    } else {
        long_break = completed >= POMODORO_CYCLE;
        total_ms = (uint32_t)(long_break ? cfg.long_break_min : cfg.break_min) * 60000UL;
    }
    remaining = total_ms;
    done      = false;
    last_ms   = millis();
    pulse_stop();
    paint(true);
}

static void finish_block(void) {
    remaining = 0;
    done      = true;
    if (cur_mode == MODE_WORK) {
        if (completed < POMODORO_CYCLE) completed++;
        last_completed_ms = millis();
    } else if (long_break) {
        completed = 0;
    }
    Serial.printf("Pomodoro: %s finished (%u/%d)\n",
                  cur_mode == MODE_WORK ? "focus" : long_break ? "long break" : "break",
                  completed, POMODORO_CYCLE);
    sound_hal_play(settings_sound().end_sound);
    pulse_start();
    paint(true);
}

static void enter(int mode) {
    cur_mode = mode;

    if (completed && millis() - last_completed_ms > CYCLE_STALE_MS) completed = 0;

    const lv_color_t col = mode_color();
    lv_obj_set_style_arc_color(arc, col, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(mode_lbl, col, 0);
    if (clawd) splash_mini_play(mode == MODE_WORK ? FOCUS_ANIM : BREAK_ANIM);

    // Always a fresh block: the side you land on is a decision made now, not a
    // bookmark into the last one.
    start_block();

    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    s_active = true;
    idle_note_activity();
}

static void leave(void) {
    cur_mode = -1;
    if (!s_active) return;
    pulse_stop();
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
    start_block();
    idle_note_activity();
}

void pomodoro_set_claude_waiting(bool waiting) {
    if (waiting == claude_waiting) return;
    claude_waiting = waiting;
    if (s_active) paint(true);
}

void pomodoro_set_codex_waiting(bool waiting) {
    if (waiting == codex_waiting) return;
    codex_waiting = waiting;
    if (s_active) paint(true);
}

// ---- Config --------------------------------------------------------------------

static void load_config(void) {
    Preferences prefs;
    prefs.begin("clawdmeter", true);
    cfg.enabled        = prefs.getUChar("pomo_on", cfg.enabled ? 1 : 0) != 0;
    cfg.focus_min      = prefs.getUChar("pomo_fmin", cfg.focus_min);
    cfg.break_min      = prefs.getUChar("pomo_bmin", cfg.break_min);
    cfg.focus_quad     = prefs.getUChar("pomo_fq", cfg.focus_quad);
    cfg.long_break_min = prefs.getUChar("pomo_lmin", cfg.long_break_min);
    prefs.end();

    cfg.focus_min      = pomodoro_clamp_focus(cfg.focus_min);
    cfg.break_min      = pomodoro_clamp_break(cfg.break_min);
    cfg.long_break_min = pomodoro_clamp_long(cfg.long_break_min);
    cfg.focus_quad    &= 3;
    Serial.printf("Pomodoro: %s, focus %u min on side %u, break %u / %u min\n",
                  cfg.enabled ? "on" : "off", cfg.focus_min, cfg.focus_quad,
                  cfg.break_min, cfg.long_break_min);
}

void pomodoro_save_config(void) {
    Preferences prefs;
    prefs.begin("clawdmeter", false);
    prefs.putUChar("pomo_on", cfg.enabled ? 1 : 0);
    prefs.putUChar("pomo_fmin", cfg.focus_min);
    prefs.putUChar("pomo_bmin", cfg.break_min);
    prefs.putUChar("pomo_fq", cfg.focus_quad);
    prefs.putUChar("pomo_lmin", cfg.long_break_min);
    prefs.end();
}

const PomodoroConfig& pomodoro_config(void) { return cfg; }

void pomodoro_set_config(const PomodoroConfig& c) { cfg = c; }

void pomodoro_set_suspended(bool s) {
    suspended = s;
    if (s) leave();
}

// ---- Build ---------------------------------------------------------------------

void pomodoro_init(lv_obj_t* parent) {
    const BoardCaps& c = board_caps();
    if (!c.has_imu) return;

    load_config();

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
    lv_obj_add_event_cb(root, tap_cb, LV_EVENT_SHORT_CLICKED, NULL);

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
    // The ring is decoration; presses belong to the overlay underneath it.
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    clawd = splash_mini_create(root, FOCUS_ANIM, CLAWD_PX);
    if (clawd) {
        // Feet on a fixed line, so switching animations doesn't make him hop.
        lv_obj_align(clawd, LV_ALIGN_BOTTOM_MID, 0, -(c.height / 2 + 44));
        lv_obj_clear_flag(clawd, LV_OBJ_FLAG_CLICKABLE);
    }

    mode_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(mode_lbl, &font_styrene_24, 0);
    lv_obj_set_style_text_color(mode_lbl, THEME_ACCENT, 0);
    lv_label_set_text(mode_lbl, "FOCUS");
    lv_obj_align(mode_lbl, LV_ALIGN_CENTER, 0, -24);

    time_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(time_lbl, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(time_lbl, THEME_TEXT, 0);
    lv_label_set_text(time_lbl, "25:00");
    lv_obj_align(time_lbl, LV_ALIGN_CENTER, 0, 26);

    const int dots_w = POMODORO_CYCLE * DOT_SIZE + (POMODORO_CYCLE - 1) * DOT_GAP;
    for (int i = 0; i < POMODORO_CYCLE; i++) {
        lv_obj_t* d = lv_obj_create(root);
        lv_obj_set_size(d, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, DOT_EMPTY, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(d, LV_ALIGN_CENTER, -dots_w / 2 + DOT_SIZE / 2 + i * (DOT_SIZE + DOT_GAP), 80);
        dots[i] = d;
    }

    hint_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(hint_lbl, &font_styrene_16, 0);
    lv_obj_set_style_text_color(hint_lbl, THEME_DIM, 0);
    lv_label_set_text(hint_lbl, "Tap to restart");
    lv_obj_align(hint_lbl, LV_ALIGN_CENTER, 0, 114);
}

lv_obj_t* pomodoro_get_root(void) { return root; }

void pomodoro_tick(void) {
    if (!root) return;

    const uint8_t q = imu_hal_rotation_quadrant();
    int want = -1;
    if (cfg.enabled && !suspended) {
        if      (q == cfg.focus_quad)             want = MODE_WORK;
        else if (q == ((cfg.focus_quad + 2) & 3)) want = MODE_BREAK;
    }

    if (want != cur_mode) {
        if (want < 0) leave();
        else          enter(want);
        return;                     // the transition already painted
    }
    if (cur_mode < 0) return;

    splash_mini_tick();

    const uint32_t now     = millis();
    const uint32_t elapsed = now - last_ms;
    last_ms = now;

    if (!done) {
        if (elapsed >= remaining) {
            finish_block();
            return;
        }
        remaining -= elapsed;
        // A block in progress keeps the panel awake; the idle timer only ever
        // sees touches and buttons otherwise.
        idle_note_activity();
    }

    paint(false);
}

bool pomodoro_is_active(void) { return s_active; }
