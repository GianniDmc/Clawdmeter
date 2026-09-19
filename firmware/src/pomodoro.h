#pragma once
#include <lvgl.h>

// Orientation-driven Pomodoro, the hourglass gesture: tip the device onto one
// side to run a focus block, onto the other for a break, stand it back upright
// to pause. The overlay only exists while the device is on a side, so the
// splash and usage screens keep the behaviour they always had.
//
// Boards without an IMU never see it: imu_hal_rotation_quadrant() is pinned at
// 0 there, which is the upright/paused case.

void pomodoro_init(lv_obj_t* parent);

// Reads the orientation, advances the running block, repaints on the second.
void pomodoro_tick(void);

// True while the overlay is up. splash_tick() stands still for it the way it
// does for the charge overlay — the direct-draw boards paint onto the panel
// behind LVGL and would scribble over it.
bool pomodoro_is_active(void);

// Restart the block currently on screen. Wired to a tap and to PWR.
void pomodoro_restart(void);
