#include "tool_screens.h"
#include "idle.h"
#include "pomodoro.h"
#include "settings.h"
#include "hal/sound_hal.h"
#include "hal/board_caps.h"
#include <Arduino.h>
#include <string.h>

LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_18);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_12);

#define MARGIN 20

// ---- Palettes ------------------------------------------------------------------
// Codex: its terminal UI — near-black, white type, grey chrome.
#define CX_BG      lv_color_hex(0x000000)
#define CX_PANEL   lv_color_hex(0x0a0a0a)
#define CX_BORDER  lv_color_hex(0x2e2e2e)
#define CX_TEXT    lv_color_hex(0xf5f5f5)
#define CX_DIM     lv_color_hex(0x8a8a8a)
#define CX_TRACK   lv_color_hex(0x333333)
#define CX_WARN    lv_color_hex(0xf5a524)
#define CX_OK      lv_color_hex(0x3fb950)

// Copilot: GitHub's dark theme, Copilot purple as the accent.
#define GH_BG      lv_color_hex(0x0d1117)
#define GH_CARD    lv_color_hex(0x161b22)
#define GH_BORDER  lv_color_hex(0x30363d)
#define GH_TEXT    lv_color_hex(0xe6edf3)
#define GH_MUTED   lv_color_hex(0x7d8590)
#define GH_ACCENT  lv_color_hex(0xa371f7)
#define GH_EMPTY   lv_color_hex(0x21262d)
static const uint32_t GH_GREENS[4] = { 0x0e4429, 0x006d32, 0x26a641, 0x39d353 };

static const char* const MONTHS[12] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};
static const char* const MONTHS_SHORT[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// ---- Shared bits ---------------------------------------------------------------

static lv_obj_t* make_page(lv_obj_t* parent, lv_color_t bg,
                           lv_event_cb_t click_cb, lv_event_cb_t long_press_cb) {
    const BoardCaps& c = board_caps();
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_size(p, c.width, c.height);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(p, click_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(p, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    return p;
}

static lv_obj_t* make_box(lv_obj_t* parent, int x, int y, int w, int h,
                          lv_color_t bg, lv_color_t border, int radius) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, border, 0);
    lv_obj_set_style_border_width(b, lv_color_eq(border, bg) ? 0 : 1, 0);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);    // taps reach the page
    return b;
}

static lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t col,
                            int x, int y, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void fmt_reset(char* buf, size_t n, int mins) {
    if (mins < 0)            snprintf(buf, n, "resets: --");
    else if (mins < 60)      snprintf(buf, n, "resets in %dm", mins);
    else if (mins < 24 * 60) snprintf(buf, n, "resets in %dh %02dm", mins / 60, mins % 60);
    else                     snprintf(buf, n, "resets in %dd %dh", mins / (24 * 60), (mins / 60) % 24);
}

// 8147 -> "8 147": French grouping. A plain space — the fonts are ASCII-only,
// so no narrow no-break space.
static void fmt_thousands(char* buf, size_t n, uint32_t v) {
    if (v >= 1000000) snprintf(buf, n, "%lu %03lu %03lu", (unsigned long)(v / 1000000),
                               (unsigned long)((v / 1000) % 1000), (unsigned long)(v % 1000));
    else if (v >= 1000) snprintf(buf, n, "%lu %03lu", (unsigned long)(v / 1000), (unsigned long)(v % 1000));
    else                snprintf(buf, n, "%lu", (unsigned long)v);
}

// ---- Codex page ----------------------------------------------------------------

struct CodexPanel {
    lv_obj_t* pct;
    lv_obj_t* bar;
    lv_obj_t* reset;
};

static lv_obj_t*  cx_page = nullptr;
static CodexPanel cx_panels[2];
static lv_obj_t*  cx_status = nullptr;
static char       cx_state[8] = "";
static bool       cx_switched = false;   // we moved the user here for a "wait"

static void build_codex_panel(lv_obj_t* page, int y, const char* title, CodexPanel* out) {
    const int w = board_caps().width - 2 * MARGIN;
    lv_obj_t* box = make_box(page, MARGIN, y, w, 140, CX_PANEL, CX_BORDER, 6);
    make_label(box, &font_mono_18, CX_DIM, 16, 12, title);
    out->pct = make_label(box, &font_mono_32, CX_TEXT, 16, 38, "--% left");

    out->bar = lv_bar_create(box);
    lv_obj_set_pos(out->bar, 16, 84);
    lv_obj_set_size(out->bar, w - 32, 10);
    lv_bar_set_range(out->bar, 0, 100);
    lv_bar_set_value(out->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(out->bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(out->bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(out->bar, CX_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(out->bar, CX_TEXT, LV_PART_INDICATOR);
    lv_obj_clear_flag(out->bar, LV_OBJ_FLAG_CLICKABLE);

    out->reset = make_label(box, &font_mono_18, CX_DIM, 16, 104, "no data yet");
}

static void paint_codex_panel(CodexPanel& p, int used, int reset_mins) {
    int left = 100 - used;
    if (left < 0) left = 0;
    lv_label_set_text_fmt(p.pct, "%d%% left", left);
    lv_bar_set_value(p.bar, left, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(p.bar, left <= 10 ? CX_WARN : CX_TEXT, LV_PART_INDICATOR);
    char buf[32];
    fmt_reset(buf, sizeof(buf), reset_mins);
    lv_label_set_text(p.reset, buf);
}

static void build_codex(lv_obj_t* parent, lv_event_cb_t click_cb, lv_event_cb_t long_cb) {
    cx_page = make_page(parent, CX_BG, click_cb, long_cb);
    make_label(cx_page, &font_mono_32, CX_TEXT, MARGIN, 26, ">_ codex");
    make_label(cx_page, &font_mono_18, CX_DIM, MARGIN + 2, 70, "rate limits");
    build_codex_panel(cx_page, 110, "5h limit", &cx_panels[0]);
    build_codex_panel(cx_page, 266, "weekly limit", &cx_panels[1]);
    cx_status = make_label(cx_page, &font_mono_18, CX_DIM, MARGIN + 2, 428, "- idle");
}

static void paint_codex_status(void) {
    static const char spin[4] = { '|', '/', '-', '\\' };
    const uint8_t frame = (uint8_t)((millis() / 150) & 3);
    if (strcmp(cx_state, "work") == 0) {
        lv_label_set_text_fmt(cx_status, "> working %c", spin[frame]);
        lv_obj_set_style_text_color(cx_status, CX_TEXT, 0);
    } else if (strcmp(cx_state, "wait") == 0) {
        // Blink: the one state that needs the user.
        lv_label_set_text(cx_status, (millis() / 500) & 1 ? "! approval needed" : "");
        lv_obj_set_style_text_color(cx_status, CX_WARN, 0);
    } else if (strcmp(cx_state, "done") == 0) {
        lv_label_set_text(cx_status, "* done");
        lv_obj_set_style_text_color(cx_status, CX_OK, 0);
    } else {
        lv_label_set_text(cx_status, "- idle");
        lv_obj_set_style_text_color(cx_status, CX_DIM, 0);
    }
}

static void codex_state_changed(const char* prev) {
    const bool alerts = settings_sound().claude_alerts;
    const bool wait = strcmp(cx_state, "wait") == 0;
    pomodoro_set_codex_waiting(wait);

    if (wait) {
        if (alerts) sound_hal_play(SOUND_ALERT);
        idle_note_activity();
        if (ui_get_current_screen() != SCREEN_CODEX &&
            !pomodoro_is_active() && !settings_is_open()) {
            ui_show_screen(SCREEN_CODEX);
            cx_switched = true;
        }
    } else if (strcmp(cx_state, "done") == 0 && alerts) {
        sound_hal_play(SOUND_CHIME);
    }

    // Back to where the user was once Codex stops waiting on them.
    if (strcmp(prev, "wait") == 0 && !wait && cx_switched) {
        cx_switched = false;
        if (ui_get_current_screen() == SCREEN_CODEX) ui_show_screen(SCREEN_SPLASH);
    }
}

void tool_screens_codex(const CodexData& d) {
    if (!cx_page) return;
    if (d.has_limits) {
        paint_codex_panel(cx_panels[0], d.p, d.pr);
        paint_codex_panel(cx_panels[1], d.w, d.wr);
    }
    if (strcmp(d.st, cx_state) != 0) {
        char prev[8];
        strlcpy(prev, cx_state, sizeof(prev));
        strlcpy(cx_state, d.st, sizeof(cx_state));
        Serial.printf("Codex: %s\n", cx_state[0] ? cx_state : "idle");
        codex_state_changed(prev);
    }
    paint_codex_status();
}

// ---- Copilot page --------------------------------------------------------------

#define GRID_COLS  7
#define GRID_ROWS  6
#define CELL_W     50
#define CELL_H     22
#define CELL_GAP   6

static lv_obj_t* cp_page = nullptr;
static lv_obj_t* cp_today_n = nullptr;
static lv_obj_t* cp_today_tok = nullptr;
static lv_obj_t* cp_month_label = nullptr;
static lv_obj_t* cp_month_n = nullptr;
static lv_obj_t* cp_month_tok = nullptr;
static lv_obj_t* cp_grid_title = nullptr;
static lv_obj_t* cp_grid = nullptr;
static CopilotGrid cp_grid_data = {};
static bool      cp_grid_valid = false;

// One object that paints the whole calendar in its draw event. 42 cells as
// separate widgets, each with its own styles, overflowed LVGL's 64 KB pool.
static void grid_draw_cb(lv_event_t* e) {
    if (!cp_grid_valid) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(cp_grid, &area);
    const CopilotGrid& g = cp_grid_data;

    uint16_t peak = 0;
    for (uint8_t i = 0; i < g.n; i++) if (g.d[i] > peak) peak = g.d[i];

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 4;
    for (int i = 0; i < GRID_ROWS * GRID_COLS; i++) {
        const int day = i - g.first_wd + 1;            // 1-based day of month
        if (day < 1 || day > g.days_in_month) continue;
        lv_color_t col = GH_EMPTY;
        dsc.bg_opa = LV_OPA_COVER;
        if (day <= g.n) {
            const uint16_t v = g.d[day - 1];
            if (v && peak) {
                // Four levels relative to the busiest day, like GitHub's graph.
                int level = (v * 4 + peak - 1) / peak - 1;
                if (level < 0) level = 0;
                if (level > 3) level = 3;
                col = lv_color_hex(GH_GREENS[level]);
            }
        } else {
            dsc.bg_opa = LV_OPA_40;                    // days still to come
        }
        dsc.bg_color = col;
        // Today gets the Copilot purple outline.
        dsc.border_color = GH_ACCENT;
        dsc.border_width = (day == g.n) ? 2 : 0;
        dsc.border_opa = LV_OPA_COVER;

        const int row = i / GRID_COLS, colx = i % GRID_COLS;
        lv_area_t cell;
        cell.x1 = area.x1 + colx * (CELL_W + CELL_GAP);
        cell.y1 = area.y1 + row * (CELL_H + CELL_GAP);
        cell.x2 = cell.x1 + CELL_W - 1;
        cell.y2 = cell.y1 + CELL_H - 1;
        lv_draw_rect(layer, &dsc, &cell);
    }
}

static void build_copilot_card(lv_obj_t* page, int x, int w, const char* title,
                               lv_obj_t** out_title, lv_obj_t** out_n, lv_obj_t** out_tok) {
    lv_obj_t* card = make_box(page, x, 84, w, 120, GH_CARD, GH_BORDER, 6);
    lv_obj_t* t = make_label(card, &font_styrene_16, GH_MUTED, 14, 10, title);
    if (out_title) *out_title = t;
    *out_n = make_label(card, &font_styrene_48, GH_TEXT, 12, 30, "--");
    *out_tok = make_label(card, &font_styrene_16, GH_MUTED, 14, 90, "AI credits");
}

static void build_copilot(lv_obj_t* parent, lv_event_cb_t click_cb, lv_event_cb_t long_cb) {
    const BoardCaps& c = board_caps();
    cp_page = make_page(parent, GH_BG, click_cb, long_cb);

    // Title: the Copilot accent as a small mark, then the name.
    lv_obj_t* mark = make_box(cp_page, MARGIN, 32, 14, 14, GH_ACCENT, GH_ACCENT, 4);
    (void)mark;
    make_label(cp_page, &font_styrene_28, GH_TEXT, MARGIN + 24, 22, "Copilot");
    make_label(cp_page, &font_styrene_16, GH_MUTED, MARGIN + 24, 56, "AI credits, no monthly limit");

    const int gap = 12;
    const int card_w = (c.width - 2 * MARGIN - gap) / 2;
    build_copilot_card(cp_page, MARGIN, card_w, "Today", nullptr, &cp_today_n, &cp_today_tok);
    build_copilot_card(cp_page, MARGIN + card_w + gap, card_w, "This month",
                       &cp_month_label, &cp_month_n, &cp_month_tok);

    // The month as a calendar, shaded like a contribution graph.
    lv_obj_t* card = make_box(cp_page, MARGIN, 216, c.width - 2 * MARGIN, 244, GH_CARD, GH_BORDER, 6);
    cp_grid_title = make_label(card, &font_styrene_16, GH_MUTED, 14, 10, "This month");
    const int grid_w = GRID_COLS * CELL_W + (GRID_COLS - 1) * CELL_GAP;
    const int x0 = (c.width - 2 * MARGIN - grid_w) / 2;
    static const char* const WD[GRID_COLS] = { "M", "T", "W", "T", "F", "S", "S" };
    for (int col = 0; col < GRID_COLS; col++) {
        lv_obj_t* l = make_label(card, &font_styrene_12, GH_MUTED,
                                 x0 + col * (CELL_W + CELL_GAP) + CELL_W / 2 - 4, 38, WD[col]);
        (void)l;
    }
    cp_grid = lv_obj_create(card);
    lv_obj_remove_style_all(cp_grid);
    lv_obj_set_pos(cp_grid, x0, 58);
    lv_obj_set_size(cp_grid, grid_w, GRID_ROWS * (CELL_H + CELL_GAP) - CELL_GAP);
    lv_obj_clear_flag(cp_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cp_grid, grid_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

void tool_screens_copilot(const CopilotData& d) {
    if (!cp_page) return;
    char buf[32];
    fmt_thousands(buf, sizeof(buf), d.tc);
    lv_label_set_text(cp_today_n, buf);
    lv_label_set_text_fmt(cp_today_tok, "credits - %d prompt%s", d.td, d.td == 1 ? "" : "s");
    fmt_thousands(buf, sizeof(buf), d.mc);
    lv_label_set_text(cp_month_n, buf);
    lv_label_set_text_fmt(cp_month_tok, "credits - %d prompt%s", d.md, d.md == 1 ? "" : "s");
}

void tool_screens_copilot_grid(const CopilotGrid& g) {
    if (!cp_page || g.month < 1 || g.month > 12) return;
    lv_label_set_text_fmt(cp_grid_title, "%s", MONTHS[g.month - 1]);
    lv_label_set_text_fmt(cp_month_label, "Since %s 1", MONTHS_SHORT[g.month - 1]);

    cp_grid_data = g;
    cp_grid_valid = true;
    lv_obj_invalidate(cp_grid);
}

// ---- Shared ---------------------------------------------------------------------

void tool_screens_init(lv_obj_t* parent, lv_event_cb_t click_cb, lv_event_cb_t long_press_cb) {
    build_codex(parent, click_cb, long_press_cb);
    build_copilot(parent, click_cb, long_press_cb);
}

void tool_screens_show(screen_t screen) {
    if (!cx_page) return;
    if (screen == SCREEN_CODEX)   lv_obj_clear_flag(cx_page, LV_OBJ_FLAG_HIDDEN);
    else                          lv_obj_add_flag(cx_page, LV_OBJ_FLAG_HIDDEN);
    if (screen == SCREEN_COPILOT) lv_obj_clear_flag(cp_page, LV_OBJ_FLAG_HIDDEN);
    else                          lv_obj_add_flag(cp_page, LV_OBJ_FLAG_HIDDEN);
}

void tool_screens_tick(void) {
    if (!cx_page || lv_obj_has_flag(cx_page, LV_OBJ_FLAG_HIDDEN)) return;
    static uint32_t last = 0;
    if (millis() - last < 150) return;
    last = millis();
    if (cx_state[0]) paint_codex_status();
}
