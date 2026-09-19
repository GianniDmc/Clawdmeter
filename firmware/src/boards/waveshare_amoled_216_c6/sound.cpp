#include "../../hal/sound_hal.h"
#include "board.h"

#if BOARD_HAS_SOUND

#include <Arduino.h>
#include "../../chime.h"

// C6 AMOLED-2.16: ES8311 codec + speaker, same shared chime engine as the S3
// sibling (../../chime.cpp). The difference is the amp: nothing gates it on
// this board, so there is no enable hook to hand over.

void sound_hal_init(void) {
    const ChimeConfig cfg = {
        SND_I2S_MCLK, SND_I2S_BCLK, SND_I2S_WS, SND_I2S_DOUT, SND_I2S_DIN,
        SND_SAMPLE_RATE, SND_ES8311_ADDR, 65, nullptr
    };
    chime_init(cfg);
}

void sound_hal_play_reset(void) { chime_play(); }
void sound_hal_tick(void)       { chime_tick(); }

#else

void sound_hal_init(void) {}
void sound_hal_tick(void) {}
void sound_hal_play_reset(void) {}

#endif  // BOARD_HAS_SOUND
