#include "multicore_display.h"

#ifdef ENABLE_OLED

#include <string.h>
#include "config.h"

// Published waveform buffers (shared across cores).
static volatile uint32_t g_wave_gen;
static uint8_t g_wave_y[2][MULTICORE_WAVEFORM_WIDTH];

// Capture state (core1 only).
static uint8_t g_capture_y[MULTICORE_WAVEFORM_WIDTH];
static uint8_t g_capture_fill;
static uint8_t g_capture_decim;

static inline uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t) v;
}

static inline uint8_t sample_to_y(float s) {
    // Clamp to [-1, 1]
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    // Map: +1 -> top (0), -1 -> bottom (OLED_HEIGHT-1)
    float yf = (-s + 1.0f) * 0.5f * (float) (OLED_HEIGHT - 1);
    return clamp_u8((int) (yf + 0.5f));
}

void multicore_display_capture_interleaved(const float *lr_interleaved, size_t frames) {
    if (!lr_interleaved || frames == 0) return;

    // Target ~30 FPS for 128px width at 48kHz:
    // decim ~= 48000 / (30*128) ≈ 12.5  -> use 12
    const uint8_t decim_n = 12;

    for (size_t i = 0; i < frames; i++) {
        g_capture_decim++;
        if (g_capture_decim < decim_n) continue;
        g_capture_decim = 0;

        float sL = lr_interleaved[i * 2u + 0u];
        g_capture_y[g_capture_fill++] = sample_to_y(sL);

        if (g_capture_fill >= MULTICORE_WAVEFORM_WIDTH) {
            uint32_t gen = g_wave_gen;
            uint32_t back = (gen + 1u) & 1u;
            memcpy(g_wave_y[back], g_capture_y, MULTICORE_WAVEFORM_WIDTH);
            __sync_synchronize(); // publish barrier
            g_wave_gen = gen + 1u;
            g_capture_fill = 0;
        }
    }
}

bool multicore_display_read_latest(uint8_t out_y[MULTICORE_WAVEFORM_WIDTH]) {
    if (!out_y) return false;

    // Try once; OLED redraw is periodic, so occasional retry isn't necessary.
    uint32_t gen = g_wave_gen;
    if (gen == 0) return false;

    __sync_synchronize();
    memcpy(out_y, g_wave_y[gen & 1u], MULTICORE_WAVEFORM_WIDTH);
    __sync_synchronize();

    return true;
}

#else

void multicore_display_capture_interleaved(const float *lr_interleaved, size_t frames) {
    (void) lr_interleaved;
    (void) frames;
}

bool multicore_display_read_latest(uint8_t out_y[MULTICORE_WAVEFORM_WIDTH]) {
    (void) out_y;
    return false;
}

#endif // ENABLE_OLED

