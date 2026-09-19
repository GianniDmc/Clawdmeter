#include "settings.h"
#include "pomodoro.h"
#include "splash.h"
#include "theme.h"
#include "idle.h"
#include "hal/imu_hal.h"
#include "hal/sound_hal.h"
#include "hal/board_caps.h"
#include <Arduino.h>
#include <Preferences.h>

LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_16);

// Geometry tuned for the 480x480 panels; everything hangs off these so a
// smaller board only needs different numbers.
#define PAD_X      36
#define TAB_Y      18
#define TAB_H      52
#define TAB_GAP    6
#define TAB_PAD_X  20     // same corner clearance as the rest of the UI
#define PAGE_Y     (TAB_Y + TAB_H + 16)
#define ROW_STEP   64
#define ROW_H      56
#define STEP_W     60      // the - and + buttons
#define VALUE_W    118
#define TOGGLE_W   120

#define FOCUS_STEP_MIN   5
#define BREAK_STEP_MIN   1
#define LONG_STEP_MIN    5
#define VOLUME_STEP      10

static const char* const SOUND_NAMES[SOUND_COUNT] = { "Bell", "Chime", "Beep", "Alert" };

static SoundConfig snd = { 60, SOUND_CHIME, true };
static bool buttons_to_host = false;

static lv_obj_t* root       = nullptr;
static lv_obj_t* page       = nullptr;     // container the make_* helpers build into

enum { TAB_POMODORO, TAB_SOUND, TAB_BUTTONS, TAB_COUNT };
static const char* const TAB_NAMES[TAB_COUNT] = { "Pomodoro", "Sound", "Buttons" };
static const int16_t     TAB_W[TAB_COUNT]     = { 132, 92, 106 };    // + OK fills the row
static lv_obj_t* pages[TAB_COUNT];
static lv_obj_t* tabs[TAB_COUNT];
static int       cur_tab = TAB_POMODORO;
static lv_obj_t* pomo_btn   = nullptr;
static lv_obj_t* pomo_lbl   = nullptr;
static lv_obj_t* alert_btn  = nullptr;
static lv_obj_t* alert_lbl  = nullptr;
static lv_obj_t* host_btn   = nullptr;
static lv_obj_t* host_lbl   = nullptr;
static lv_obj_t* focus_val  = nullptr;
static lv_obj_t* break_val  = nullptr;
static lv_obj_t* long_val   = nullptr;
static lv_obj_t* volume_val = nullptr;
static lv_obj_t* sound_val  = nullptr;
static lv_obj_t* side_hint  = nullptr;
static bool      s_open     = false;
static int8_t    shown_quad = -1;
static int8_t    shown_fq   = -1;

enum { FIELD_FOCUS, FIELD_BREAK, FIELD_LONG, FIELD_VOLUME, FIELD_SOUND };

// ---- Preferences -------------------------------------------------------------

void settings_load(void) {
    Preferences prefs;
    prefs.begin("clawdmeter", true);
    snd.volume        = prefs.getUChar("snd_vol", snd.volume);
    snd.end_sound     = prefs.getUChar("snd_end", snd.end_sound);
    snd.claude_alerts = prefs.getUChar("cc_alert", snd.claude_alerts ? 1 : 0) != 0;
    buttons_to_host   = prefs.getUChar("btn_host", buttons_to_host ? 1 : 0) != 0;
    prefs.end();

    if (snd.volume > 100) snd.volume = 100;
    if (snd.end_sound >= SOUND_COUNT) snd.end_sound = SOUND_CHIME;
    sound_hal_set_volume(snd.volume);
}

static void save_sound(void) {
    Preferences prefs;
    prefs.begin("clawdmeter", false);
    prefs.putUChar("snd_vol", snd.volume);
    prefs.putUChar("snd_end", snd.end_sound);
    prefs.putUChar("cc_alert", snd.claude_alerts ? 1 : 0);
    prefs.putUChar("btn_host", buttons_to_host ? 1 : 0);
    prefs.end();
}

const SoundConfig& settings_sound(void) { return snd; }

// Boards without the settings page (no IMU) can't flip this back, so they keep
// the upstream behaviour: buttons are a keyboard for the host.
bool settings_buttons_to_host(void) { return buttons_to_host || !root; }

// ---- Widgets -----------------------------------------------------------------

static lv_obj_t* make_button(int x, int y, int w, int h, const char* text,
                             const lv_font_t* font, lv_obj_t** out_label) {
    lv_obj_t* b = lv_obj_create(page);
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

static void make_row_label(int y, const char* text) {
    lv_obj_t* l = lv_label_create(page);
    lv_obj_set_style_text_font(l, &font_styrene_28, 0);
    lv_obj_set_style_text_color(l, THEME_TEXT, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, PAD_X, y + (ROW_H - 28) / 2 - 2);
}

static void paint_toggle(lv_obj_t* btn, lv_obj_t* lbl, bool on) {
    lv_label_set_text(lbl, on ? "ON" : "OFF");
    lv_obj_set_style_bg_color(btn, on ? THEME_ACCENT : THEME_BAR_BG, 0);
    lv_obj_set_style_text_color(lbl, on ? THEME_TEXT : THEME_DIM, 0);
}

static void refresh(void) {
    const PomodoroConfig& c = pomodoro_config();
    paint_toggle(pomo_btn, pomo_lbl, c.enabled);
    paint_toggle(alert_btn, alert_lbl, snd.claude_alerts);
    paint_toggle(host_btn, host_lbl, buttons_to_host);

    lv_label_set_text_fmt(focus_val,  "%u min", c.focus_min);
    lv_label_set_text_fmt(break_val,  "%u min", c.break_min);
    lv_label_set_text_fmt(long_val,   "%u min", c.long_break_min);
    lv_label_set_text_fmt(volume_val, "%u%%", snd.volume);
    lv_label_set_text(sound_val, SOUND_NAMES[snd.end_sound]);

    shown_quad = -1;    // force the side hint to repaint
}

// ---- Callbacks ---------------------------------------------------------------

static void pomo_toggle_cb(lv_event_t* e) {
    (void)e;
    PomodoroConfig c = pomodoro_config();
    c.enabled = !c.enabled;
    pomodoro_set_config(c);
    refresh();
}

static void alert_toggle_cb(lv_event_t* e) {
    (void)e;
    snd.claude_alerts = !snd.claude_alerts;
    if (snd.claude_alerts) sound_hal_play(SOUND_ALERT);
    refresh();
}

static void host_toggle_cb(lv_event_t* e) {
    (void)e;
    buttons_to_host = !buttons_to_host;
    refresh();
}

// user_data packs the field above bit 0 and the direction in bit 0.
static void step_cb(lv_event_t* e) {
    const uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    const int field = (int)(tag >> 1);
    const int dir   = (tag & 1) ? +1 : -1;

    PomodoroConfig c = pomodoro_config();
    switch (field) {
    case FIELD_FOCUS: c.focus_min      = pomodoro_clamp_focus(c.focus_min + dir * FOCUS_STEP_MIN); break;
    case FIELD_BREAK: c.break_min      = pomodoro_clamp_break(c.break_min + dir * BREAK_STEP_MIN); break;
    case FIELD_LONG:  c.long_break_min = pomodoro_clamp_long(c.long_break_min + dir * LONG_STEP_MIN); break;
    case FIELD_VOLUME: {
        const int v = snd.volume + dir * VOLUME_STEP;
        snd.volume = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
        sound_hal_set_volume(snd.volume);
        // Only preview on a deliberate tap; a held button would stack plays.
        if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) sound_hal_play(snd.end_sound);
        break;
    }
    case FIELD_SOUND:
        snd.end_sound = (uint8_t)((snd.end_sound + SOUND_COUNT + dir) % SOUND_COUNT);
        sound_hal_play(snd.end_sound);
        break;
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

static void make_stepper(int y, int field, const char* minus_txt, const char* plus_txt,
                         bool repeat, lv_obj_t** out_value) {
    const int right   = board_caps().width - PAD_X;
    const int plus_x  = right - STEP_W;
    const int value_x = plus_x - VALUE_W;
    const int minus_x = value_x - STEP_W;

    lv_obj_t* minus = make_button(minus_x, y, STEP_W, ROW_H, minus_txt, &font_styrene_28, nullptr);
    lv_obj_t* plus  = make_button(plus_x,  y, STEP_W, ROW_H, plus_txt,  &font_styrene_28, nullptr);
    // SHORT_CLICKED rather than PRESSED so a long press can repeat instead.
    lv_obj_add_event_cb(minus, step_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)((field << 1) | 0));
    lv_obj_add_event_cb(plus,  step_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)((field << 1) | 1));
    if (repeat) {
        const lv_event_code_t codes[] = { LV_EVENT_LONG_PRESSED, LV_EVENT_LONG_PRESSED_REPEAT };
        for (lv_event_code_t code : codes) {
            lv_obj_add_event_cb(minus, step_cb, code, (void*)(uintptr_t)((field << 1) | 0));
            lv_obj_add_event_cb(plus,  step_cb, code, (void*)(uintptr_t)((field << 1) | 1));
        }
    }

    lv_obj_t* v = lv_label_create(page);
    lv_obj_set_width(v, VALUE_W);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(v, &font_styrene_24, 0);
    lv_obj_set_style_text_color(v, THEME_TEXT, 0);
    lv_obj_set_pos(v, value_x, y + (ROW_H - 24) / 2 - 2);
    *out_value = v;
}

static lv_obj_t* make_toggle(int y, lv_event_cb_t cb, lv_obj_t** out_label) {
    lv_obj_t* b = make_button(board_caps().width - PAD_X - TOGGLE_W, y, TOGGLE_W, ROW_H,
                              "ON", &font_styrene_24, out_label);
    lv_obj_add_event_cb(b, cb, LV_EVENT_SHORT_CLICKED, NULL);
    return b;
}

// ---- Page --------------------------------------------------------------------
// Tabs, not one long scrolling page: the C6 has to repaint the whole panel for
// every scrolled frame and managed ~10 fps. A tab switch is one repaint.

static lv_obj_t* make_hint(int y, const char* text) {
    lv_obj_t* l = lv_label_create(page);
    lv_obj_set_width(l, board_caps().width - 2 * PAD_X);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, &font_styrene_16, 0);
    lv_obj_set_style_text_color(l, THEME_DIM, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, PAD_X, y);
    return l;
}

static void show_tab(int t) {
    cur_tab = t;
    for (int i = 0; i < TAB_COUNT; i++) {
        const bool on = (i == t);
        if (on) lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(tabs[i], on ? THEME_BAR_BG : THEME_BG, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(tabs[i], 0), on ? THEME_TEXT : THEME_DIM, 0);
    }
}

static void tab_cb(lv_event_t* e) {
    show_tab((int)(uintptr_t)lv_event_get_user_data(e));
}

static lv_obj_t* make_page(void) {
    const BoardCaps& c = board_caps();
    lv_obj_t* p = lv_obj_create(root);
    lv_obj_set_pos(p, 0, PAGE_Y);
    lv_obj_set_size(p, c.width, c.height - PAGE_Y);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

void settings_init(lv_obj_t* parent) {
    const BoardCaps& c = board_caps();
    // The Pomodoro needs the IMU, and it is most of what lives here.
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

    // Tab row: three tabs, then OK filling what is left.
    page = root;
    int x = TAB_PAD_X;
    for (int i = 0; i < TAB_COUNT; i++) {
        tabs[i] = make_button(x, TAB_Y, TAB_W[i], TAB_H, TAB_NAMES[i], &font_styrene_24, nullptr);
        lv_obj_add_event_cb(tabs[i], tab_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i);
        x += TAB_W[i] + TAB_GAP;
    }
    lv_obj_t* ok = make_button(x, TAB_Y, c.width - TAB_PAD_X - x, TAB_H, "OK", &font_styrene_24, nullptr);
    lv_obj_set_style_bg_color(ok, THEME_ACCENT, 0);
    lv_obj_add_event_cb(ok, done_cb, LV_EVENT_SHORT_CLICKED, NULL);

    const int content_w = c.width - 2 * PAD_X;
    int y;

    // -- Pomodoro
    page = pages[TAB_POMODORO] = make_page();
    y = 0;
    make_row_label(y, "Enabled");
    pomo_btn = make_toggle(y, pomo_toggle_cb, &pomo_lbl);
    y += ROW_STEP;
    make_row_label(y, "Focus");
    make_stepper(y, FIELD_FOCUS, "-", "+", true, &focus_val);
    y += ROW_STEP;
    make_row_label(y, "Break");
    make_stepper(y, FIELD_BREAK, "-", "+", true, &break_val);
    y += ROW_STEP;
    make_row_label(y, "Long");
    make_stepper(y, FIELD_LONG, "-", "+", true, &long_val);
    y += ROW_H + 4;
    lv_obj_t* long_hint = make_hint(y, "");
    lv_label_set_text_fmt(long_hint, "Long break after every %d focus blocks", POMODORO_CYCLE);
    y += 30;
    lv_obj_t* side = make_button(PAD_X, y, content_w, ROW_H,
                                 "Use this side for focus", &font_styrene_24, nullptr);
    lv_obj_add_event_cb(side, side_cb, LV_EVENT_SHORT_CLICKED, NULL);
    y += ROW_H + 8;
    side_hint = make_hint(y, "");
    lv_obj_set_style_text_align(side_hint, LV_TEXT_ALIGN_CENTER, 0);

    // -- Sound
    page = pages[TAB_SOUND] = make_page();
    y = 0;
    make_row_label(y, "Volume");
    make_stepper(y, FIELD_VOLUME, "-", "+", false, &volume_val);
    y += ROW_STEP;
    make_row_label(y, "End");
    make_stepper(y, FIELD_SOUND, "<", ">", false, &sound_val);
    y += ROW_STEP;
    make_row_label(y, "Claude");
    alert_btn = make_toggle(y, alert_toggle_cb, &alert_lbl);
    y += ROW_H + 4;
    make_hint(y, "Sound when Claude Code needs you or is done");

    // -- Buttons
    page = pages[TAB_BUTTONS] = make_page();
    y = 0;
    make_row_label(y, "To the Mac");
    host_btn = make_toggle(y, host_toggle_cb, &host_lbl);
    y += ROW_H + 4;
    make_hint(y, "On: Space and Shift+Tab for Claude Code.\n"
                 "Off: BOOT switches screens, KEY opens settings.");

    page = nullptr;
    show_tab(TAB_POMODORO);
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
    save_sound();
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
    pomodoro_set_suspended(false);
    splash_request_full_redraw();
    idle_note_activity();
}

bool settings_is_open(void) { return s_open; }

void settings_show_tab(int tab) {
    if (root && tab >= 0 && tab < TAB_COUNT) show_tab(tab);
}
