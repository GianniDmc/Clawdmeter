#pragma once

// Optional audio output (a passive piezo buzzer driven by LEDC PWM). Used to
// chime when the Claude session limit resets and when a Pomodoro block ends.
// Boards without working audio — e.g. the C6 AMOLED-1.8, whose amp path is
// unverified — no-op on init/tick and ignore play requests.
//
// Playback is non-blocking: sound_hal_play_reset() only *queues* the chime and
// returns immediately; sound_hal_tick() (called every loop) advances the notes
// so the LVGL render loop never stalls.

void sound_hal_init(void);
void sound_hal_tick(void);
void sound_hal_play_reset(void);
