#pragma once
#include <lvgl.h>

// Settings page, opened by a long press on any screen. Today it only holds the
// Pomodoro: on/off, focus and break lengths, and which side is focus.
//
// Edits apply live and are written to NVS once, on close.

void settings_init(lv_obj_t* parent);
void settings_tick(void);

void settings_open(void);
void settings_close(void);

// splash_tick() stands still while this is up, like for the other overlays.
bool settings_is_open(void);
