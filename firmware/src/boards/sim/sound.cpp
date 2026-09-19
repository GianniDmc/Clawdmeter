#include "../../hal/sound_hal.h"
#include <stdio.h>

void sound_hal_init(void) {}
void sound_hal_tick(void) {}
void sound_hal_play_reset(void) { printf("[sim] chime! (session reset)\n"); }
void sound_hal_play(uint8_t sound)       { printf("[sim] sound %u\n", sound); }
void sound_hal_set_volume(uint8_t volume) { printf("[sim] volume %u\n", volume); }
