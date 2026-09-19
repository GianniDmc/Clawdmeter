#pragma once
#include <lvgl.h>
#include <stdint.h>

// Settings page, opened by a long press on any screen (or KEY): tabs for the
// Pomodoro (on/off, lengths, focus side), sound (volume, end sound, Claude
// alerts) and buttons. OK, PWR or KEY closes it.
//
// Edits apply live and are written to NVS once, on close.

// Sound settings live here rather than in the sound HAL: they are user
// preferences, and every board with a speaker shares them.
struct SoundConfig {
    uint8_t volume;          // 0..100, 0 = silent
    uint8_t end_sound;       // SOUND_* played when a Pomodoro block ends
    bool    claude_alerts;   // sound when Claude Code needs you / finishes
};

// Loads saved preferences and applies the volume. Runs on every board, before
// the page itself is built (which only happens on boards with an IMU).
void settings_load(void);
const SoundConfig& settings_sound(void);

// true: BOOT / KEY act as a Bluetooth keyboard for the host (Space, Shift+Tab
// for Claude Code). false (default): they drive the device itself.
bool settings_buttons_to_host(void);

void settings_init(lv_obj_t* parent);
void settings_tick(void);

void settings_open(void);
void settings_close(void);

// Jump to a tab (0 Pomodoro, 1 Sound, 2 Buttons); the simulator uses it for
// headless screenshots.
void settings_show_tab(int tab);

// splash_tick() stands still while this is up, like for the other overlays.
bool settings_is_open(void);
