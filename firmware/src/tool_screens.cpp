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
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_12);

#define GRID_COLS  7
#define GRID_ROWS  6

// Layout picked from the board's pixel size, the way ui.cpp does it: one
// breakpoint at 460 px tall, everything else derived so a 240x240 or 368x448
// panel gets the same pages, just tighter.
struct ToolLayout {
    int16_t w, h, margin;
    const lv_font_t *cx_title, *cx_body, *cx_pct;      // Codex: mono
    const lv_font_t *cp_title, *cp_num, *cp_label, *cp_wd;   // Copilot: Styrene
    int16_t title_y, clock_y, sub_y;
    int16_t panels_y, panel_h, panel_gap, status_y;
    int16_t cards_y, card_h, cal_y, cal_h, cell_w, cell_h, cell_gap, grid_top;
    bool    show_clock;
};
static ToolLayout T;

static void compute_tool_layout(const BoardCaps& c) {
    const bool big = c.height >= 460;
    T.w = c.width;
    T.h = c.height;
    T.margin   = big ? 20 : 10;
    T.cx_title = big ? &font_mono_32 : &font_mono_18;
    T.cx_pct   = big ? &font_mono_32 : &font_mono_18;
    T.cx_body  = &font_mono_18;
    T.cp_title = big ? &font_styrene_28 : &font_styrene_20;
    T.cp_num   = big ? &font_styrene_48 : &font_styrene_24;
    T.cp_label = big ? &font_styrene_16 : &font_styrene_12;
    T.cp_wd    = &font_styrene_12;

    T.title_y  = big ? 26 : 8;
    T.clock_y  = T.title_y + (big ? 10 : 2);
    T.sub_y    = big ? 70 : 34;

    // Codex: two panels between the header and the status line.
    T.status_y  = c.height - (big ? 52 : 24);
    T.panels_y  = T.sub_y + (big ? 40 : 20);
    T.panel_gap = big ? 16 : 8;
    T.panel_h   = (T.status_y - T.panels_y - T.panel_gap - (big ? 12 : 6)) / 2;

    // Copilot: two cards, then the calendar filling what is left.
    T.cards_y = T.sub_y + (big ? 28 : 16);
    T.card_h  = big ? 120 : (c.height / 4);
    T.cal_y   = T.cards_y + T.card_h + (big ? 12 : 8);
    T.cal_h   = c.height - T.cal_y - T.margin;
    T.cell_gap = big ? 6 : 3;
    T.grid_top = big ? 58 : 34;
    // Narrow panels have no room between the title and the battery: the usage
    // page still carries the clock.
    T.show_clock = c.width >= 400;
    const int16_t inner = c.width - 2 * T.margin - 2 * (big ? 14 : 8);
    T.cell_w = (inner - (GRID_COLS - 1) * T.cell_gap) / GRID_COLS;
    T.cell_h = (T.cal_h - T.grid_top - (big ? 8 : 4) - (GRID_ROWS - 1) * T.cell_gap) / GRID_ROWS;
}

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
static lv_obj_t*  cx_clock = nullptr;
static char       cx_state[8] = "";
static bool       cx_seen = false;    // a "cx" message ever arrived
static screen_t   cx_back = SCREEN_COUNT;  // screen to restore after a "wait"

static void build_codex_panel(lv_obj_t* page, int y, const char* title, CodexPanel* out) {
    const int w = T.w - 2 * T.margin;
    const int h = T.panel_h;
    const int pad = T.margin < 20 ? 8 : 16;
    lv_obj_t* box = make_box(page, T.margin, y, w, h, CX_PANEL, CX_BORDER, 6);
    make_label(box, T.cx_body, CX_DIM, pad, h * 12 / 140, title);
    out->pct = make_label(box, T.cx_pct, CX_TEXT, pad, h * 38 / 140, "--% left");

    out->bar = lv_bar_create(box);
    lv_obj_set_pos(out->bar, pad, h - (T.margin < 20 ? 32 : 56));
    lv_obj_set_size(out->bar, w - 2 * pad, T.margin < 20 ? 6 : 10);
    lv_bar_set_range(out->bar, 0, 100);
    lv_bar_set_value(out->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(out->bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(out->bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(out->bar, CX_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(out->bar, CX_TEXT, LV_PART_INDICATOR);
    lv_obj_clear_flag(out->bar, LV_OBJ_FLAG_CLICKABLE);

    out->reset = make_label(box, T.cx_body, CX_DIM, pad, h - (T.margin < 20 ? 20 : 36), "no data yet");
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
    make_label(cx_page, T.cx_title, CX_TEXT, T.margin, T.title_y, ">_ codex");
    make_label(cx_page, T.cx_body, CX_DIM, T.margin + 2, T.sub_y, "rate limits");
    build_codex_panel(cx_page, T.panels_y, "5h limit", &cx_panels[0]);
    build_codex_panel(cx_page, T.panels_y + T.panel_h + T.panel_gap, "weekly limit", &cx_panels[1]);
    cx_status = make_label(cx_page, T.cx_body, CX_DIM, T.margin + 2, T.status_y, "- idle");
    cx_clock = make_label(cx_page, T.cx_body, CX_DIM, 0, T.clock_y, "");
    lv_obj_align(cx_clock, LV_ALIGN_TOP_MID, 0, T.clock_y);
    if (!T.show_clock) lv_obj_add_flag(cx_clock, LV_OBJ_FLAG_HIDDEN);
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

// A tool's live state changed: waiting on the user sounds the alert, wakes
// the panel and brings its page up (then goes back once answered); a finished
// turn chimes. Same rules as Claude Code's (claude_state.cpp).
static void tool_state_changed(const char* st, const char* prev, const char* name,
                               screen_t page, screen_t* back_to,
                               void (*set_waiting)(bool)) {
    const bool alerts = settings_sound().claude_alerts;
    const bool wait = strcmp(st, "wait") == 0;
    set_waiting(wait);

    if (wait) {
        if (alerts) sound_hal_play(SOUND_ALERT);
        idle_note_activity();
        if (ui_get_current_screen() != page &&
            !pomodoro_is_active() && !settings_is_open()) {
            *back_to = ui_get_current_screen();      // restore it once answered
            ui_show_screen(page);
        }
    } else if (strcmp(st, "done") == 0) {
        if (alerts) sound_hal_play(settings_sound().end_sound);
        pomodoro_note_tool_done(name);
    }

    if (strcmp(prev, "wait") == 0 && !wait && *back_to != SCREEN_COUNT) {
        const screen_t back = *back_to;
        *back_to = SCREEN_COUNT;                     // SCREEN_COUNT = we did not switch
        if (ui_get_current_screen() == page) ui_show_screen(back);
    }
}

// Same warning as the usage page: the last heads-up before a window runs out.
#define CX_WARN_PCT   90
#define CX_REARM_PCT  85
static bool cx_warned[2] = { false, false };

static void codex_limit_warning(int used, bool* warned, const char* what) {
    if (used < CX_REARM_PCT) *warned = false;
    if (used < CX_WARN_PCT || *warned) return;
    *warned = true;
    Serial.printf("Codex %s limit at %d%%\n", what, used);
    if (settings_sound().claude_alerts) sound_hal_play(SOUND_ALERT);
    idle_note_activity();
}

void tool_screens_codex(const CodexData& d) {
    if (!cx_page) return;
    cx_seen = true;
    if (d.has_limits) {
        paint_codex_panel(cx_panels[0], d.p, d.pr);
        paint_codex_panel(cx_panels[1], d.w, d.wr);
        codex_limit_warning(d.p, &cx_warned[0], "5h");
        codex_limit_warning(d.w, &cx_warned[1], "weekly");
    }
    if (strcmp(d.st, cx_state) != 0) {
        char prev[8];
        strlcpy(prev, cx_state, sizeof(prev));
        strlcpy(cx_state, d.st, sizeof(cx_state));
        Serial.printf("Codex: %s\n", cx_state[0] ? cx_state : "idle");
        tool_state_changed(cx_state, prev, "Codex", SCREEN_CODEX, &cx_back,
                           pomodoro_set_codex_waiting);
    }
    paint_codex_status();
}

// ---- Copilot page --------------------------------------------------------------

static lv_obj_t* cp_page = nullptr;
static lv_obj_t* cp_today_n = nullptr;
static lv_obj_t* cp_today_tok = nullptr;
static lv_obj_t* cp_month_label = nullptr;
static lv_obj_t* cp_month_n = nullptr;
static lv_obj_t* cp_month_tok = nullptr;
static lv_obj_t* cp_grid_title = nullptr;
static lv_obj_t* cp_caveat = nullptr;     // "+N CLI sessions not counted"
static lv_obj_t* cp_grid = nullptr;
static CopilotGrid cp_grid_data = {};
static bool      cp_grid_valid = false;
static lv_obj_t* cp_clock = nullptr;
static lv_obj_t* cp_dot = nullptr;       // status row: OpenCode's live state
static lv_obj_t* cp_status = nullptr;
static char      oc_state[8] = "";
static bool      cp_seen = false;     // a "cp"/"cpg"/"ocs" message ever arrived
static screen_t  cp_back = SCREEN_COUNT;

#define GH_WARN    lv_color_hex(0xd29922)   // GitHub's attention yellow
#define GH_OK      lv_color_hex(0x3fb950)

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
        cell.x1 = area.x1 + colx * (T.cell_w + T.cell_gap);
        cell.y1 = area.y1 + row * (T.cell_h + T.cell_gap);
        cell.x2 = cell.x1 + T.cell_w - 1;
        cell.y2 = cell.y1 + T.cell_h - 1;
        lv_draw_rect(layer, &dsc, &cell);
    }
}

static void build_copilot_card(lv_obj_t* page, int x, int w, const char* title,
                               lv_obj_t** out_title, lv_obj_t** out_n, lv_obj_t** out_tok) {
    const int pad = T.margin < 20 ? 8 : 14;
    lv_obj_t* card = make_box(page, x, T.cards_y, w, T.card_h, GH_CARD, GH_BORDER, 6);
    lv_obj_t* t = make_label(card, T.cp_label, GH_MUTED, pad, T.card_h * 10 / 120, title);
    if (out_title) *out_title = t;
    *out_n = make_label(card, T.cp_num, GH_TEXT, pad - 2, T.card_h * 30 / 120, "--");
    *out_tok = make_label(card, T.cp_label, GH_MUTED, pad, T.card_h * 90 / 120, "AI credits");
}

static void build_copilot(lv_obj_t* parent, lv_event_cb_t click_cb, lv_event_cb_t long_cb) {
    cp_page = make_page(parent, GH_BG, click_cb, long_cb);
    const int pad = T.margin < 20 ? 8 : 14;

    // Title: the Copilot accent as a small mark, then the name.
    lv_obj_t* mark = make_box(cp_page, T.margin, T.title_y + 10, 14, 14, GH_ACCENT, GH_ACCENT, 4);
    (void)mark;
    make_label(cp_page, T.cp_title, GH_TEXT, T.margin + 24, T.title_y, "Copilot");
    // What OpenCode is doing, as a GitHub-style status dot and label.
    cp_dot = make_box(cp_page, T.margin + 3, T.sub_y + 5, 8, 8, GH_MUTED, GH_MUTED, 4);
    cp_status = make_label(cp_page, T.cp_label, GH_MUTED, T.margin + 24, T.sub_y, "OpenCode idle");
    cp_clock = make_label(cp_page, T.cp_label, GH_MUTED, 0, T.clock_y, "");
    lv_obj_align(cp_clock, LV_ALIGN_TOP_MID, 0, T.clock_y);
    if (!T.show_clock) lv_obj_add_flag(cp_clock, LV_OBJ_FLAG_HIDDEN);

    const int gap = T.margin < 20 ? 8 : 12;
    const int card_w = (T.w - 2 * T.margin - gap) / 2;
    build_copilot_card(cp_page, T.margin, card_w, "Today", nullptr, &cp_today_n, &cp_today_tok);
    build_copilot_card(cp_page, T.margin + card_w + gap, card_w, "This month",
                       &cp_month_label, &cp_month_n, &cp_month_tok);

    // The month as a calendar, shaded like a contribution graph.
    lv_obj_t* card = make_box(cp_page, T.margin, T.cal_y, T.w - 2 * T.margin, T.cal_h,
                              GH_CARD, GH_BORDER, 6);
    cp_grid_title = make_label(card, T.cp_label, GH_MUTED, pad, T.cal_h * 10 / 244, "This month");
    // The CLI records sessions but no longer its credits, so say what is
    // missing instead of passing OpenCode's share off as the whole bill.
    cp_caveat = make_label(card, T.cp_wd, GH_MUTED, pad, T.cal_h * 10 / 244, "");
    lv_obj_align(cp_caveat, LV_ALIGN_TOP_RIGHT, -pad, T.cal_h * 10 / 244 + 2);
    lv_obj_add_flag(cp_caveat, LV_OBJ_FLAG_HIDDEN);
    const int grid_w = GRID_COLS * T.cell_w + (GRID_COLS - 1) * T.cell_gap;
    const int x0 = (T.w - 2 * T.margin - grid_w) / 2;
    static const char* const WD[GRID_COLS] = { "M", "T", "W", "T", "F", "S", "S" };
    for (int col = 0; col < GRID_COLS; col++) {
        lv_obj_t* l = make_label(card, T.cp_wd, GH_MUTED,
                                 x0 + col * (T.cell_w + T.cell_gap) + T.cell_w / 2 - 4,
                                 T.grid_top - (T.margin < 20 ? 14 : 20), WD[col]);
        (void)l;
    }
    cp_grid = lv_obj_create(card);
    lv_obj_remove_style_all(cp_grid);
    lv_obj_set_pos(cp_grid, x0, T.grid_top);
    lv_obj_set_size(cp_grid, grid_w, GRID_ROWS * (T.cell_h + T.cell_gap) - T.cell_gap);
    lv_obj_clear_flag(cp_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cp_grid, grid_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

void tool_screens_copilot(const CopilotData& d) {
    if (!cp_page) return;
    cp_seen = true;
    char buf[32];
    fmt_thousands(buf, sizeof(buf), d.tc);
    lv_label_set_text(cp_today_n, buf);
    lv_label_set_text_fmt(cp_today_tok, T.show_clock ? "credits - %d prompt%s" : "%d prompt%s",
                          d.td, d.td == 1 ? "" : "s");
    fmt_thousands(buf, sizeof(buf), d.mc);
    lv_label_set_text(cp_month_n, buf);
    lv_label_set_text_fmt(cp_month_tok, T.show_clock ? "credits - %d prompt%s" : "%d prompt%s",
                          d.md, d.md == 1 ? "" : "s");
    if (d.u > 0) {
        lv_label_set_text_fmt(cp_caveat, "+ %d CLI session%s not counted", d.u, d.u == 1 ? "" : "s");
        lv_obj_clear_flag(cp_caveat, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cp_caveat, LV_OBJ_FLAG_HIDDEN);
    }
}

void tool_screens_copilot_grid(const CopilotGrid& g) {
    if (!cp_page || g.month < 1 || g.month > 12) return;
    lv_label_set_text_fmt(cp_grid_title, "%s", MONTHS[g.month - 1]);
    lv_label_set_text_fmt(cp_month_label, "Since %s 1", MONTHS_SHORT[g.month - 1]);

    cp_grid_data = g;
    cp_grid_valid = true;
    lv_obj_invalidate(cp_grid);
}

static void paint_copilot_status(void) {
    lv_color_t col = GH_MUTED;
    const char* text = "OpenCode idle";
    lv_opa_t dot_opa = LV_OPA_COVER;
    if (strcmp(oc_state, "work") == 0) {
        col = GH_ACCENT;
        text = "OpenCode working...";
        // Breathing dot, like GitHub's in-progress badge.
        dot_opa = (millis() / 500) & 1 ? LV_OPA_COVER : LV_OPA_50;
    } else if (strcmp(oc_state, "wait") == 0) {
        // Blink: the one state that needs the user.
        col = GH_WARN;
        text = "OpenCode needs your input";
        dot_opa = (millis() / 500) & 1 ? LV_OPA_COVER : LV_OPA_20;
    } else if (strcmp(oc_state, "done") == 0) {
        col = GH_OK;
        text = "OpenCode done";
    }
    if (text) lv_label_set_text(cp_status, text);
    lv_obj_set_style_text_color(cp_status, strcmp(oc_state, "") == 0 ? GH_MUTED : GH_TEXT, 0);
    lv_obj_set_style_bg_color(cp_dot, col, 0);
    lv_obj_set_style_bg_opa(cp_dot, dot_opa, 0);
}

void tool_screens_opencode_state(const char* st) {
    if (!cp_page) return;
    if (st[0]) cp_seen = true;
    if (strcmp(st, oc_state) != 0) {
        char prev[8];
        strlcpy(prev, oc_state, sizeof(prev));
        strlcpy(oc_state, st, sizeof(oc_state));
        Serial.printf("OpenCode: %s\n", oc_state[0] ? oc_state : "idle");
        tool_state_changed(oc_state, prev, "OpenCode", SCREEN_COPILOT, &cp_back,
                           pomodoro_set_opencode_waiting);
    }
    paint_copilot_status();
}

// ---- Shared ---------------------------------------------------------------------

void tool_screens_init(lv_obj_t* parent, lv_event_cb_t click_cb, lv_event_cb_t long_press_cb) {
    compute_tool_layout(board_caps());
    build_codex(parent, click_cb, long_press_cb);
    build_copilot(parent, click_cb, long_press_cb);
}

bool tool_screens_has_data(screen_t screen) {
    if (screen == SCREEN_CODEX)   return cx_seen;
    if (screen == SCREEN_COPILOT) return cp_seen;
    return true;
}

void tool_screens_show(screen_t screen) {
    if (!cx_page) return;
    if (screen == SCREEN_CODEX)   lv_obj_clear_flag(cx_page, LV_OBJ_FLAG_HIDDEN);
    else                          lv_obj_add_flag(cx_page, LV_OBJ_FLAG_HIDDEN);
    if (screen == SCREEN_COPILOT) lv_obj_clear_flag(cp_page, LV_OBJ_FLAG_HIDDEN);
    else                          lv_obj_add_flag(cp_page, LV_OBJ_FLAG_HIDDEN);
}

void tool_screens_tick(void) {
    if (!cx_page) return;
    static uint32_t last = 0;
    if (millis() - last < 150) return;
    last = millis();
    char clock[12];
    ui_clock_text(clock, sizeof(clock));
    if (!lv_obj_has_flag(cx_page, LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(cx_clock, clock);
        if (cx_state[0]) paint_codex_status();
    }
    if (!lv_obj_has_flag(cp_page, LV_OBJ_FLAG_HIDDEN)) lv_label_set_text(cp_clock, clock);
    if ((strcmp(oc_state, "work") == 0 || strcmp(oc_state, "wait") == 0) &&
        !lv_obj_has_flag(cp_page, LV_OBJ_FLAG_HIDDEN)) paint_copilot_status();
}
