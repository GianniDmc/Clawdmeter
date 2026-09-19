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
// One-slot mailbox of sound ids. The player task blocks on it; a new request
// overwrites whatever is queued, and a sound in progress checks it between
// chunks so a newer request cuts it short instead of being dropped.
static QueueHandle_t   requests = nullptr;

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
// Integer only. The C6 has no FPU: a soft-float sinf() per sample could not
// keep up with 44.1 kHz, the I2S DMA ran dry, and the tones came out torn.
// A 256-entry sine table stepped by a 32-bit phase accumulator, with a Q16
// exponential decay, costs a few integer ops per sample on any core.
struct Note { uint16_t freq; uint16_t ms; };

static const Note SOUND_CHIME_NOTES[] = { {659, 170}, {988, 420} };             // E5 -> B5
static const Note SOUND_BEEP_NOTES[]  = { {880, 90}, {0, 70}, {880, 90}, {0, 70}, {880, 90} };
static const Note SOUND_ALERT_NOTES[] = { {784, 120}, {0, 40}, {1047, 120}, {0, 40}, {1319, 260} }; // G5 C6 E6

#define SYNTH_CHUNK  256                     // stereo frames per write
#define SYNTH_PEAK   9000                    // below the bell clip's level; no clipping
#define ATTACK_MS    5
#define RELEASE_MS   8                       // fade the tail so notes don't click

static int16_t sine_table[256];

static bool interrupted(void) {
    return requests && uxQueueMessagesWaiting(requests) > 0;
}

static bool synth_note(const Note& n) {
    static int16_t buf[SYNTH_CHUNK * 2];
    const uint32_t rate    = (uint32_t)cfg.sample_rate;
    const uint32_t total   = rate * n.ms / 1000;
    const uint32_t attack  = rate * ATTACK_MS / 1000;
    const uint32_t release = rate * RELEASE_MS / 1000;
    const uint32_t inc     = (uint32_t)(((uint64_t)n.freq << 32) / rate);
    // Per-sample decay factor so the note ends ~-40 dB down; one expf per note.
    const uint32_t decay_q16 = (uint32_t)(expf(-4.6f / (float)(total ? total : 1)) * 65536.0f);
    uint32_t phase = 0;
    uint32_t amp   = (uint32_t)SYNTH_PEAK << 16;     // Q16
    uint32_t i = 0;
    while (i < total) {
        if (interrupted()) return false;
        const uint32_t count = (total - i < SYNTH_CHUNK) ? total - i : SYNTH_CHUNK;
        for (uint32_t k = 0; k < count; k++, i++) {
            int32_t s = 0;
            if (n.freq) {
                int32_t env = (int32_t)(amp >> 16);
                if (i < attack)             env = env * (int32_t)i / (int32_t)attack;
                if (total - i < release)    env = env * (int32_t)(total - i) / (int32_t)release;
                // Linear interpolation between table entries: a bare lookup
                // leaves a faint buzz on the higher notes.
                const int32_t a = sine_table[phase >> 24];
                const int32_t b = sine_table[((phase >> 24) + 1) & 0xFF];
                const int32_t wave = a + (((b - a) * (int32_t)((phase >> 8) & 0xFFFF)) >> 16);
                s = (wave * env) >> 15;
                phase += inc;
                if (i >= attack) amp = (uint32_t)(((uint64_t)amp * decay_q16) >> 16);
            }
            buf[k * 2] = buf[k * 2 + 1] = (int16_t)s;
        }
        i2s.write((uint8_t*)buf, count * 2 * sizeof(int16_t));
    }
    return true;
}

static void play_notes(const Note* notes, size_t count) {
    for (size_t k = 0; k < count; k++) {
        if (!synth_note(notes[k])) return;
    }
}

// The embedded clip, in chunks for the same reason: a newer request wins.
static void play_bell(void) {
    const size_t chunk = 4096;
    for (size_t off = 0; off < bell_pcm_len; off += chunk) {
        if (interrupted()) return;
        const size_t n = (bell_pcm_len - off < chunk) ? bell_pcm_len - off : chunk;
        i2s.write((uint8_t*)bell_pcm + off, n);
    }
}

static void play(uint8_t id) {
    switch (id) {
    case SOUND_CHIME: play_notes(SOUND_CHIME_NOTES, sizeof(SOUND_CHIME_NOTES) / sizeof(Note)); break;
    case SOUND_BEEP:  play_notes(SOUND_BEEP_NOTES,  sizeof(SOUND_BEEP_NOTES)  / sizeof(Note)); break;
    case SOUND_ALERT: play_notes(SOUND_ALERT_NOTES, sizeof(SOUND_ALERT_NOTES) / sizeof(Note)); break;
    default:          play_bell(); break;
    }
}

// Flush the DMA ring with silence, so an interrupted sound doesn't leave its
// last buffers to loop and the next one starts from quiet.
static void write_silence(uint32_t ms) {
    static const int16_t zeros[SYNTH_CHUNK * 2] = {0};
    uint32_t frames = (uint32_t)cfg.sample_rate * ms / 1000;
    while (frames) {
        const uint32_t n = frames < SYNTH_CHUNK ? frames : SYNTH_CHUNK;
        i2s.write((uint8_t*)zeros, n * 2 * sizeof(int16_t));
        frames -= n;
    }
}

static void chime_task(void* arg) {
    (void)arg;
    uint8_t id;
    for (;;) {
        xQueueReceive(requests, &id, portMAX_DELAY);
        if (cfg.amp_enable) cfg.amp_enable(true);
        delay(8);                              // let the amp settle (avoids turn-on pop)
        // Chain requests that arrive while playing without cycling the amp.
        do {
            play(id);
            write_silence(20);
        } while (xQueueReceive(requests, &id, 0) == pdTRUE);
        if (cfg.amp_enable) cfg.amp_enable(false);
    }
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
    for (int k = 0; k < 256; k++) {
        sine_table[k] = (int16_t)lroundf(32767.0f * sinf(2.0f * (float)M_PI * k / 256.0f));
    }
    requests = xQueueCreate(1, sizeof(uint8_t));
    // Above the Arduino loop task (1): the feeder must never starve the DMA.
    // It spends nearly all its time blocked in i2s.write(), so the UI keeps up.
    if (!requests || xTaskCreatePinnedToCore(chime_task, "chime", 4096, nullptr, 5, nullptr, 0) != pdPASS) {
        Serial.println("chime: player task failed");
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
    if (!ready || cfg.volume == 0) return;
    xQueueOverwrite(requests, &id);
}

void chime_tick(void) {}   // playback runs in chime_task; nothing to poll
