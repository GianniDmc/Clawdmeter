#pragma once
#include <lvgl.h>
#include <stdint.h>

// Orientation-driven Pomodoro, the hourglass gesture: stand the device on the
// focus side and a focus block runs, on the opposite side a break, on either
// of the two remaining sides the normal screens come back. Every landing on a
// side starts a fresh block; leaving it drops whatever was left.
//
// Boards without an IMU never see it: imu_hal_rotation_quadrant() is pinned at
// 0 there, and the overlay is not even built.

struct PomodoroConfig {
    bool    enabled;
    uint8_t focus_min;
    uint8_t break_min;
    uint8_t focus_quad;     // 0..3 from imu_hal_rotation_quadrant(); break is opposite
    uint8_t long_break_min; // replaces the break after every POMODORO_CYCLE focus blocks
};

#define POMODORO_CYCLE           4

#define POMODORO_FOCUS_MIN_MIN   5
#define POMODORO_FOCUS_MIN_MAX   120
#define POMODORO_BREAK_MIN_MIN   1
#define POMODORO_BREAK_MIN_MAX   60
#define POMODORO_LONG_MIN_MIN    5
#define POMODORO_LONG_MIN_MAX    60

static inline uint8_t pomodoro_clamp_focus(int v) {
    return (uint8_t)(v < POMODORO_FOCUS_MIN_MIN ? POMODORO_FOCUS_MIN_MIN
                   : v > POMODORO_FOCUS_MIN_MAX ? POMODORO_FOCUS_MIN_MAX : v);
}
static inline uint8_t pomodoro_clamp_break(int v) {
    return (uint8_t)(v < POMODORO_BREAK_MIN_MIN ? POMODORO_BREAK_MIN_MIN
                   : v > POMODORO_BREAK_MIN_MAX ? POMODORO_BREAK_MIN_MAX : v);
}
static inline uint8_t pomodoro_clamp_long(int v) {
    return (uint8_t)(v < POMODORO_LONG_MIN_MIN ? POMODORO_LONG_MIN_MIN
                   : v > POMODORO_LONG_MIN_MAX ? POMODORO_LONG_MIN_MAX : v);
}

// Loads the saved config from NVS and builds the (hidden) overlay.
void pomodoro_init(lv_obj_t* parent);

// Reads the orientation, advances the running block, repaints on the second.
void pomodoro_tick(void);

// True while the overlay is up. splash_tick() stands still for it the way it
// does for the charge overlay — the direct-draw boards paint onto the panel
// behind LVGL and would scribble over it.
bool pomodoro_is_active(void);

// The overlay object, for callers that hang extra gestures on it (null on
// boards without an IMU).
lv_obj_t* pomodoro_get_root(void);

// Restart the block currently on screen. Wired to a tap and to PWR.
void pomodoro_restart(void);

// Settings. set_config() applies immediately; save() writes NVS, so callers
// batch edits and save once (the settings page does it on close).
const PomodoroConfig& pomodoro_config(void);
void pomodoro_set_config(const PomodoroConfig& cfg);
void pomodoro_save_config(void);

// Claude Code is waiting for the user: the timer screen says so, since it
// covers the splash that would otherwise show it.
void pomodoro_set_claude_waiting(bool waiting);
void pomodoro_set_codex_waiting(bool waiting);
void pomodoro_set_opencode_waiting(bool waiting);

// While suspended the overlay never shows, whatever the orientation — the
// settings page needs the device on its side without a block starting.
void pomodoro_set_suspended(bool suspended);
