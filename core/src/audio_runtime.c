/**
 * Audio runtime (I2S output + core1 DSP loop).
 */

#include "audio_runtime.h"

#include "config.h"
#include "crosscore_bus.h"
#include "HvHeavy.h"
#include <math.h>
#include <string.h>

#ifdef ENABLE_OLED
#include "multicore_display.h"
#endif

#ifdef ENABLE_USB_AUDIO
#include "usb/usb_audio.h"
#endif

#include "pico/audio.h"
#include "pico/audio_i2s.h"
#include "pico/multicore.h"
#include "pico/time.h"

#define AUDIO_RUNTIME_SAMPLE_RATE 48000u
#define AUDIO_RUNTIME_BLOCK_SIZE 64u  /* Heavy default block size */
#define AUDIO_RUNTIME_BUFFER_SIZE (AUDIO_RUNTIME_BLOCK_SIZE * 2u)  /* Stereo */
#define AUDIO_INT16_MAX_VALUE 32767.0f

#ifdef ENABLE_USB_AUDIO
#define AUDIO_RUNTIME_PRODUCER_BUFFER_COUNT AUDIO_RUNTIME_USB_PRODUCER_BUFFER_COUNT
#else
#define AUDIO_RUNTIME_PRODUCER_BUFFER_COUNT 3u
#endif

static const audio_format_t audio_format = {
    .sample_freq = AUDIO_RUNTIME_SAMPLE_RATE,
    .format = AUDIO_BUFFER_FORMAT_PCM_S16,
    .channel_count = 2
};

static audio_buffer_format_t audio_buffer_format = {
    .format = &audio_format,
    .sample_stride = 4  /* 2 bytes per sample * 2 channels */
};

static const audio_i2s_config_t i2s_config = {
    .data_pin = PICO_AUDIO_I2S_DATA_PIN,
    .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
    .dma_channel = 0,
    .pio_sm = 0
};

static audio_buffer_pool_t *audio_pool;
static HeavyContextInterface *runtime_context;
static bool core1_started;
static uint32_t g_core1_dsp_avg_permille;

static inline float sanitize_audio_sample(float sample) {
    // Safety guard: block NaN/Inf propagation into PCM conversion.
    if (!isfinite(sample)) {
        return 0.0f;
    }
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    return sample;
}

static void int16_to_float(const int16_t *in, float *out, size_t count) {
    for (size_t i = 0; i < count; i++) {
        out[i] = (float)in[i] / 32768.0f;
    }
}

static void float_to_int16(const float *in, int16_t *out, size_t count) {
    for (size_t i = 0; i < count; i++) {
        float sample = sanitize_audio_sample(in[i]);
        out[i] = (int16_t) (sample * AUDIO_INT16_MAX_VALUE);
    }
}

static void audio_core1_main(void) {
    static float audio_out_buffer[AUDIO_RUNTIME_BUFFER_SIZE];
    static float audio_in_buffer[AUDIO_RUNTIME_BUFFER_SIZE];
#ifdef ENABLE_USB_AUDIO
    static int16_t usb_in_i16[AUDIO_RUNTIME_BUFFER_SIZE];
    static bool usb_in_primed;
#endif
    static int hv_in_ch = -1;
    static int hv_out_ch = -1;
    uint64_t dsp_accum_process_us = 0;
    uint32_t dsp_accum_frames = 0;
    while (true) {
        if (audio_pool == NULL || runtime_context == NULL) {
            continue;
        }
        if (hv_in_ch < 0 || hv_out_ch < 0) {
            hv_in_ch = hv_getNumInputChannels(runtime_context);
            hv_out_ch = hv_getNumOutputChannels(runtime_context);
            if (hv_in_ch < 0) hv_in_ch = 0;
            if (hv_out_ch < 0) hv_out_ch = 0;
            if (hv_in_ch > 2) hv_in_ch = 2;
            if (hv_out_ch > 2) hv_out_ch = 2;
        }
        if ((size_t)AUDIO_RUNTIME_CTRL_DRAIN_MAX_EVENTS_PER_BLOCK == 0u) {
            crosscore_bus_ctrl_drain_to_heavy(runtime_context);
        } else {
            (void)crosscore_bus_ctrl_drain_to_heavy_budgeted(
                runtime_context,
                (size_t)AUDIO_RUNTIME_CTRL_DRAIN_MAX_EVENTS_PER_BLOCK
            );
        }
        audio_buffer_t *buffer = take_audio_buffer(audio_pool, true);
        if (buffer == NULL) {
            continue;
        }

        int16_t *samples = (int16_t *) buffer->buffer->bytes;
        size_t samples_per_channel = buffer->max_sample_count;
        size_t blocks_to_process =
            (samples_per_channel + AUDIO_RUNTIME_BLOCK_SIZE - 1u) / AUDIO_RUNTIME_BLOCK_SIZE;
        for (size_t block = 0; block < blocks_to_process; block++) {
            size_t block_size = AUDIO_RUNTIME_BLOCK_SIZE;
            if (block == blocks_to_process - 1u) {
                block_size = samples_per_channel - (block * AUDIO_RUNTIME_BLOCK_SIZE);
            }

            float *hv_in_ptr = NULL;
#ifdef ENABLE_USB_AUDIO
            if (hv_in_ch > 0) {
                if (!usb_audio_is_streaming()) {
                    usb_in_primed = false;
                }

                bool fill_with_silence = false;
                uint32_t ring_fill = usb_audio_get_ring_fill_frames();
                if (!usb_in_primed) {
                    if (ring_fill >= (uint32_t)USB_AUDIO_TARGET_FILL_FRAMES) {
                        usb_in_primed = true;
                    } else {
                        fill_with_silence = true;
                    }
                }

                if (fill_with_silence) {
                    memset(audio_in_buffer, 0, block_size * 2u * sizeof(float));
                } else {
                    size_t got_frames = usb_audio_pop_i16(usb_in_i16, block_size);
                    if (got_frames < block_size) {
                        memset(
                            usb_in_i16 + (got_frames * 2u),
                            0,
                            (block_size - got_frames) * 2u * sizeof(int16_t)
                        );
                    }
                    int16_to_float(usb_in_i16, audio_in_buffer, block_size * 2u);
                }

                if (hv_in_ch == 1) {
                    for (size_t i = 0; i < block_size; i++) {
                        float l = audio_in_buffer[(i * 2u) + 0u];
                        float r = audio_in_buffer[(i * 2u) + 1u];
                        audio_in_buffer[i] = 0.5f * (l + r);
                    }
                }

                hv_in_ptr = audio_in_buffer;
            }
#endif
            if (hv_in_ptr == NULL && hv_in_ch > 0) {
                memset(audio_in_buffer, 0, block_size * (size_t)hv_in_ch * sizeof(float));
                hv_in_ptr = audio_in_buffer;
            }

            uint64_t t0 = time_us_64();
            hv_processInlineInterleaved(runtime_context, hv_in_ptr, audio_out_buffer, block_size);
            uint64_t t1 = time_us_64();
            dsp_accum_process_us += (t1 - t0);
            dsp_accum_frames += (uint32_t) block_size;

            if (dsp_accum_frames >= AUDIO_RUNTIME_SAMPLE_RATE) {
                const uint64_t numerator =
                    dsp_accum_process_us * (uint64_t) AUDIO_RUNTIME_SAMPLE_RATE * 1000ull;
                const uint64_t denominator = (uint64_t) dsp_accum_frames * 1000000ull;
                uint32_t permille = 0u;
                if (denominator > 0u) {
                    permille = (uint32_t) (numerator / denominator);
                }
                __atomic_store_n(&g_core1_dsp_avg_permille, permille, __ATOMIC_RELAXED);
                dsp_accum_process_us = 0u;
                dsp_accum_frames = 0u;
            }

#ifdef ENABLE_OLED
            multicore_display_capture_interleaved(audio_out_buffer, block_size);
#endif
            int16_t *block_samples = samples + (block * AUDIO_RUNTIME_BLOCK_SIZE * 2u);
            if (hv_out_ch <= 0) {
                memset(block_samples, 0, block_size * 2u * sizeof(int16_t));
            } else if (hv_out_ch == 1) {
                for (size_t i = 0; i < block_size; i++) {
                    float sample = sanitize_audio_sample(audio_out_buffer[i]);
                    int16_t s16 = (int16_t) (sample * AUDIO_INT16_MAX_VALUE);
                    block_samples[(i * 2u) + 0u] = s16;
                    block_samples[(i * 2u) + 1u] = s16;
                }
            } else {
                float_to_int16(audio_out_buffer, block_samples, block_size * 2u);
            }
        }
        buffer->sample_count = samples_per_channel;
        give_audio_buffer(audio_pool, buffer);
    }
}

uint32_t audio_runtime_sample_rate(void) {
    return AUDIO_RUNTIME_SAMPLE_RATE;
}

bool audio_runtime_init_output(void) {
    if (audio_i2s_setup(&audio_format, &i2s_config) == NULL) {
        return false;
    }

    audio_pool = audio_new_producer_pool(
        &audio_buffer_format,
        AUDIO_RUNTIME_PRODUCER_BUFFER_COUNT,
        AUDIO_RUNTIME_BLOCK_SIZE
    );
    if (audio_pool == NULL) {
        return false;
    }

    if (!audio_i2s_connect(audio_pool)) {
        return false;
    }

    audio_i2s_set_enabled(true);
    return true;
}

void audio_runtime_start(struct HeavyContextInterface *ctx) {
    if (core1_started || ctx == NULL) return;
    runtime_context = (HeavyContextInterface *) ctx;
    __atomic_store_n(&g_core1_dsp_avg_permille, 0u, __ATOMIC_RELAXED);
    __sync_synchronize();
    multicore_launch_core1(audio_core1_main);
    core1_started = true;
}

uint32_t audio_runtime_get_core1_dsp_load_avg_permille(void) {
    return __atomic_load_n(&g_core1_dsp_avg_permille, __ATOMIC_RELAXED);
}
