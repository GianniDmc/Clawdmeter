#include "sound_hal.h"

// Defaults for the optional parts of the sound HAL. A board with an ES8311
// overrides both (see boards/*/sound.cpp); everything else keeps these and
// never has to know the richer API exists.

__attribute__((weak)) void sound_hal_play(uint8_t sound) {
    (void)sound;
    sound_hal_play_reset();
}

__attribute__((weak)) void sound_hal_set_volume(uint8_t volume) {
    (void)volume;
}
