#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,      // Claude
    SCREEN_CODEX,
    SCREEN_COPILOT,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);

// Wall-clock time as the usage page shows it ("14:07"), empty until the
// daemon has sent the time. The tool pages put it in their own header.
void ui_clock_text(char* buf, size_t n);
void ui_toggle_splash(void);
// Tap / BOOT: splash -> Claude -> Codex -> Copilot -> splash.
void ui_next_screen(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);
