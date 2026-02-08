/**
 * Audio runtime (I2S output + core1 DSP loop).
 */

#include "audio_runtime.h"

#include "config.h"
#include "multicore_audio.h"
#include "HvHeavy.h"

#ifdef ENABLE_OLED
#include "multicore_display.h"
#endif

#include "pico/audio.h"
#include "pico/audio_i2s.h"
#include "pico/multicore.h"

#define AUDIO_RUNTIME_SAMPLE_RATE 48000u
#define AUDIO_RUNTIME_BLOCK_SIZE 64u  /* Heavy default block size */
#define AUDIO_RUNTIME_BUFFER_SIZE (AUDIO_RUNTIME_BLOCK_SIZE * 2u)  /* Stereo */
#define AUDIO_INT16_MAX_VALUE 32767.0f

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

static void float_to_int16(const float *in, int16_t *out, size_t count) {
    for (size_t i = 0; i < count; i++) {
        float sample = in[i];
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        out[i] = (int16_t) (sample * AUDIO_INT16_MAX_VALUE);
    }
}

static void audio_core1_main(void) {
    static float audio_out_buffer[AUDIO_RUNTIME_BUFFER_SIZE];
    while (true) {
        if (audio_pool == NULL || runtime_context == NULL) {
            continue;
        }
        multicore_drain_ctrl(runtime_context);
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
            hv_processInlineInterleaved(runtime_context, NULL, audio_out_buffer, block_size);
#ifdef ENABLE_OLED
            multicore_display_capture_interleaved(audio_out_buffer, block_size);
#endif
            float_to_int16(audio_out_buffer,
                           samples + (block * AUDIO_RUNTIME_BLOCK_SIZE * 2u),
                           block_size * 2u);
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

    audio_pool = audio_new_producer_pool(&audio_buffer_format, 3, AUDIO_RUNTIME_BLOCK_SIZE);
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
    __sync_synchronize();
    multicore_launch_core1(audio_core1_main);
    core1_started = true;
}
