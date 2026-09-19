#include "claude_state.h"
#include "ui.h"
#include "idle.h"
#include "splash.h"
#include "pomodoro.h"
#include "settings.h"
#include "hal/sound_hal.h"
#include <Arduino.h>
#include <string.h>


static ClaudeActivity state = CLAUDE_IDLE;
static bool  switched_for_wait = false;   // we moved the user to the splash

static ClaudeActivity parse(const char* cc) {
    if (!cc)                     return CLAUDE_IDLE;
    if (strcmp(cc, "work") == 0) return CLAUDE_WORK;
    if (strcmp(cc, "wait") == 0) return CLAUDE_WAIT;
    if (strcmp(cc, "done") == 0) return CLAUDE_DONE;
    return CLAUDE_IDLE;
}

void claude_state_update(const char* cc) {
    const ClaudeActivity next = parse(cc);
    if (next == state) return;
    const ClaudeActivity prev = state;
    state = next;
    Serial.printf("Claude Code: %s\n", cc && cc[0] ? cc : "idle");

    const bool alerts = settings_sound().claude_alerts;
    pomodoro_set_claude_waiting(next == CLAUDE_WAIT);

    switch (next) {
    case CLAUDE_WORK:
        splash_set_override("laptop");
        break;
    case CLAUDE_WAIT:
        splash_set_override("waving");
        if (alerts) sound_hal_play(SOUND_ALERT);
        idle_note_activity();                 // wake the panel: this one matters
        // Waving on a screen nobody is looking at helps no one. The Pomodoro
        // and settings overlays show it their own way (or are being used).
        if (ui_get_current_screen() != SCREEN_SPLASH &&
            !pomodoro_is_active() && !settings_is_open()) {
            ui_show_screen(SCREEN_SPLASH);
            switched_for_wait = true;
        }
        break;
    case CLAUDE_DONE:
        splash_set_override("jumping happy");
        if (alerts) sound_hal_play(settings_sound().end_sound);
        pomodoro_note_tool_done("Claude");
        break;
    case CLAUDE_IDLE:
        splash_set_override(NULL);
        break;
    }

    // Put the user back where they were once Claude stops waiting on them.
    if (prev == CLAUDE_WAIT && next != CLAUDE_WAIT && switched_for_wait) {
        switched_for_wait = false;
        if (ui_get_current_screen() == SCREEN_SPLASH && !pomodoro_is_active()) {
            ui_toggle_splash();
        }
    }
}

ClaudeActivity claude_state_current(void) { return state; }
