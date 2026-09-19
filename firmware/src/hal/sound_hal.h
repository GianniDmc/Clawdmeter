#pragma once

// Optional audio output (a passive piezo buzzer driven by LEDC PWM). Used to
// chime when the Claude session limit resets and when a Pomodoro block ends.
// Boards without working audio — e.g. the C6 AMOLED-1.8, whose amp path is
// unverified — no-op on init/tick and ignore play requests.
//
// Playback is non-blocking: sound_hal_play_reset() only *queues* the chime and
// returns immediately; sound_hal_tick() (called every loop) advances the notes
// so the LVGL render loop never stalls.

#include <stdint.h>

// Sounds a board can be asked for. BELL is the embedded clip; the others are
// short synthesized tone patterns (see chime.cpp).
enum {
    SOUND_BELL  = 0,
    SOUND_CHIME = 1,     // two rising notes
    SOUND_BEEP  = 2,     // three short beeps
    SOUND_ALERT = 3,     // three-note rising arpeggio: "someone needs you"
    SOUND_COUNT
};

void sound_hal_init(void);
void sound_hal_tick(void);
void sound_hal_play_reset(void);

// Play any SOUND_* and set the output volume (0..100). Both have weak no-op
// defaults in sound_hal_weak.cpp — play falls back to sound_hal_play_reset() —
// so only boards with a real codec need to implement them.
void sound_hal_play(uint8_t sound);
void sound_hal_set_volume(uint8_t volume);
