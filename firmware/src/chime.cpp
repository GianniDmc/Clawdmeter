#include "chime.h"
#include <Arduino.h>
#include "ESP_I2S.h"
#include "es8311.h"
#include "bell_pcm.h"   // const uint8_t bell_pcm[] / bell_pcm_len — 44.1 kHz 16-bit stereo
#include "hal/sound_hal.h"
#include <math.h>

// Shared ES8311 chime engine. See chime.h. Adapted from the original 2.16
// sound.cpp so the 2.16, 1.8 (and any future ES8311 board) share one copy of
// the codec setup, the embedded PCM, and the non-blocking playback task.

static I2SClass        i2s;
static ChimeConfig     cfg;
static es8311_handle_t codec   = nullptr;
static bool            ready   = false;
static volatile bool   playing = false;
static volatile uint8_t pending_sound = SOUND_BELL;

static bool es8311_setup(void) {
    es8311_handle_t es = es8311_create(0, cfg.es8311_addr);   // I2C port 0 (shared Wire bus)
    if (!es) return false;
    codec = es;
    // mclk_inverted, sclk_inverted, mclk_from_mclk_pin, mclk_frequency, sample_frequency
    const es8311_clock_config_t clk = {
        false, false, true, cfg.sample_rate * 256, cfg.sample_rate
    };
    if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
    es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
    es8311_microphone_config(es, false);
    es8311_voice_volume_set(es, cfg.volume, NULL);
    return true;
}

// ---- Synthesized tones ------------------------------------------------------
// A note is a sine with a 5 ms attack and an exponential decay, written in
// small chunks so the task stack stays small. freq 0 is a rest.
struct Note { uint16_t freq; uint16_t ms; };

static const Note SOUND_CHIME_NOTES[] = { {659, 170}, {988, 420} };             // E5 -> B5
static const Note SOUND_BEEP_NOTES[]  = { {880, 90}, {0, 70}, {880, 90}, {0, 70}, {880, 90} };
static const Note SOUND_ALERT_NOTES[] = { {784, 120}, {0, 40}, {1047, 120}, {0, 40}, {1319, 260} }; // G5 C6 E6

#define SYNTH_CHUNK 256                      // stereo frames per write

static void synth_note(const Note& n) {
    static int16_t buf[SYNTH_CHUNK * 2];
    const uint32_t rate   = (uint32_t)cfg.sample_rate;
    const uint32_t total  = rate * n.ms / 1000;
    const uint32_t attack = rate * 5 / 1000;
    const float    step   = 2.0f * (float)M_PI * n.freq / (float)rate;
    const float    decay  = expf(-5.0f / (float)(total ? total : 1));   // ~-43 dB by the end
    float amp = 1.0f;
    uint32_t i = 0;
    while (i < total) {
        const uint32_t count = (total - i < SYNTH_CHUNK) ? total - i : SYNTH_CHUNK;
        for (uint32_t k = 0; k < count; k++, i++) {
            float s = 0.0f;
            if (n.freq) {
                const float env = (i < attack) ? (float)i / (float)attack : amp;
                s = sinf(step * (float)i) * env * 12000.0f;
                if (i >= attack) amp *= decay;
            }
            buf[k * 2] = buf[k * 2 + 1] = (int16_t)s;
        }
        i2s.write((uint8_t*)buf, count * 2 * sizeof(int16_t));
    }
}

static void play_notes(const Note* notes, size_t count) {
    for (size_t k = 0; k < count; k++) synth_note(notes[k]);
}

static void chime_task(void* arg) {
    if (cfg.amp_enable) cfg.amp_enable(true);
    delay(8);                                  // let the amp settle (avoids turn-on pop)
    switch (pending_sound) {
    case SOUND_CHIME: play_notes(SOUND_CHIME_NOTES, sizeof(SOUND_CHIME_NOTES) / sizeof(Note)); break;
    case SOUND_BEEP:  play_notes(SOUND_BEEP_NOTES,  sizeof(SOUND_BEEP_NOTES)  / sizeof(Note)); break;
    case SOUND_ALERT: play_notes(SOUND_ALERT_NOTES, sizeof(SOUND_ALERT_NOTES) / sizeof(Note)); break;
    default:          i2s.write((uint8_t*)bell_pcm, bell_pcm_len); break;
    }
    delay(20);
    if (cfg.amp_enable) cfg.amp_enable(false);
    playing = false;
    vTaskDelete(nullptr);
}

bool chime_init(const ChimeConfig& c) {
    cfg = c;
    if (cfg.amp_enable) cfg.amp_enable(false);   // amp off until we play

    i2s.setPins(cfg.bclk, cfg.ws, cfg.dout, cfg.din, cfg.mclk);
    if (!i2s.begin(I2S_MODE_STD, cfg.sample_rate, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("chime: I2S init failed");
        return false;
    }
    if (!es8311_setup()) {
        Serial.println("chime: ES8311 init failed");
        return false;
    }
    ready = true;
    Serial.println("chime: ES8311 ready");
    return true;
}

void chime_play(void) { chime_play_sound(SOUND_BELL); }

void chime_set_volume(uint8_t volume) {
    cfg.volume = volume > 100 ? 100 : volume;
    if (codec) es8311_voice_volume_set(codec, cfg.volume, NULL);
}

void chime_play_sound(uint8_t id) {
    if (!ready || playing || cfg.volume == 0) return;
    pending_sound = id;
    playing = true;
    if (xTaskCreatePinnedToCore(chime_task, "chime", 4096, nullptr, 1, nullptr, 0) != pdPASS)
        playing = false;   // couldn't spawn — stay silent rather than wedge the flag
}

void chime_tick(void) {}   // playback runs in chime_task; nothing to poll
