#pragma once
#include <lvgl.h>
#include <stdint.h>
#include "ui.h"

// Pages for the other coding tools, each dressed like its tool:
//   Codex   — terminal-style, monochrome: 5-hour and weekly limits as "% left",
//             plus a status line mirroring what Codex is doing (hooks).
//   Copilot — GitHub dark theme: AI credits (as GitHub's usage page counts
//             them) today and since the 1st, and the month as a calendar
//             shaded like a contribution graph.
// Fed by the daemon's "cx" / "cp" / "cpg" messages; see daemon/tool_usage.py.

struct CodexData {
    int  p, pr;          // 5-hour window: % used, minutes to reset (-1 unknown)
    int  w, wr;          // weekly window
    bool has_limits;     // false when only a state came through
    char st[8];          // "work" | "wait" | "done" | ""
};

struct CopilotData {
    uint32_t tc, mc;     // AI credits today / since the 1st
    int      td, md;     // prompts today / since the 1st
};

#define COPILOT_MAX_DAYS 31
struct CopilotGrid {
    uint8_t  month;      // 1..12
    uint8_t  first_wd;   // weekday of the 1st, 0 = Monday
    uint8_t  days_in_month;
    uint8_t  n;          // days reported so far (= today's date)
    uint16_t d[COPILOT_MAX_DAYS];   // AI credits per day
};

// Builds both pages hidden inside `parent`; taps and long presses on them go
// to the same callbacks as the other screens.
void tool_screens_init(lv_obj_t* parent, lv_event_cb_t click_cb, lv_event_cb_t long_press_cb);

// Shows the page for `screen` if it is one of ours, hides the others.
void tool_screens_show(screen_t screen);

void tool_screens_codex(const CodexData& d);
void tool_screens_copilot(const CopilotData& d);
void tool_screens_copilot_grid(const CopilotGrid& g);

// OpenCode's live state ("work" | "wait" | "done" | ""), shown on the Copilot
// page — OpenCode is how Copilot gets used here (daemon "ocs" message).
void tool_screens_opencode_state(const char* st);

// Status-line animation.
void tool_screens_tick(void);
