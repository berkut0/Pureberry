/**
 * RP2350 Pure Data Firmware
 * 
 * Main firmware entry point for running Pure Data patches compiled with hvcc.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "pico/audio.h"
#include "pico/audio_i2s.h"

// Heavy context interface
// These will be included from the generated hvcc code
// #include "Heavy_heavy.h"
// #include "HvHeavy.h"

// Audio configuration
#define SAMPLE_RATE 48000
#define AUDIO_BLOCK_SIZE 64  // Heavy default block size
#define AUDIO_BUFFER_SIZE (AUDIO_BLOCK_SIZE * 2)  // Stereo

// I2S pin configuration
#define I2S_DATA_PIN 9
#define I2S_CLOCK_PIN_BASE 10  // BCLK pin (LRCLK will be CLOCK_PIN_BASE, BCLK will be CLOCK_PIN_BASE+1)

// Audio format
static const audio_format_t audio_format = {
    .sample_freq = SAMPLE_RATE,
    .format = AUDIO_BUFFER_FORMAT_PCM_S16,  // 16-bit signed PCM
    .channel_count = 2  // Stereo
};

// Audio buffer format (needed for buffer pool creation)
static audio_buffer_format_t audio_buffer_format = {
    .format = &audio_format,
    .sample_stride = 4  // 2 bytes per sample * 2 channels = 4 bytes per frame
};

// I2S configuration
static const audio_i2s_config_t i2s_config = {
    .data_pin = I2S_DATA_PIN,
    .clock_pin_base = I2S_CLOCK_PIN_BASE,
    .dma_channel = 0,  // Will be assigned automatically
    .pio_sm = 0  // PIO state machine 0
};

// Audio buffer pool for output
static audio_buffer_pool_t *audio_pool = NULL;

// Heavy context (will be initialized from generated code)
// static HeavyContextInterface *heavy_context = NULL;

/**
 * Convert 16-bit integer samples to float (-1.0 to 1.0)
 */
static void int16_to_float(const int16_t *in, float *out, size_t count) {
    for (size_t i = 0; i < count; i++) {
        out[i] = (float)in[i] / 32768.0f;
    }
}

/**
 * Convert float samples (-1.0 to 1.0) to 16-bit integer
 */
static void float_to_int16(const float *in, int16_t *out, size_t count) {
    for (size_t i = 0; i < count; i++) {
        float sample = in[i];
        // Clamp to [-1.0, 1.0]
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        out[i] = (int16_t)(sample * 32767.0f);
    }
}

/**
 * Audio producer function - generates audio samples from Heavy processing
 */
static void audio_producer_task(void) {
    // TODO: This will be implemented to feed audio from Heavy to I2S
    // For now, this is a placeholder structure
    
    if (audio_pool == NULL) {
        return;
    }
    
    // Get a free buffer from the pool
    audio_buffer_t *buffer = take_audio_buffer(audio_pool, true);
    if (buffer == NULL) {
        return;
    }
    
    // TODO: Process audio through Heavy and fill buffer
    // For now, fill with silence
    int16_t *samples = (int16_t *)buffer->buffer->bytes;
    size_t sample_count = buffer->max_sample_count;
    
    // Convert Heavy output to int16 samples
    // float audio_out_buffer[AUDIO_BUFFER_SIZE];
    // hv_processInline(heavy_context, NULL, audio_out_buffer, AUDIO_BLOCK_SIZE);
    // float_to_int16(audio_out_buffer, samples, sample_count);
    
    // For now, fill with silence
    memset(samples, 0, sample_count * sizeof(int16_t) * 2);  // Stereo
    
    buffer->sample_count = sample_count;
    
    // Give the buffer back to the pool
    give_audio_buffer(audio_pool, buffer);
}

int main() {
    // Initialize stdio (USB serial)
    stdio_init_all();
    
    // Wait a bit for USB serial to be ready
    sleep_ms(100);
    
    printf("\n=== RP2350 Pure Data Firmware ===\n");
    printf("Initializing...\n");
    
    // Setup I2S audio output
    const audio_format_t *output_format = audio_i2s_setup(&audio_format, &i2s_config);
    if (output_format == NULL) {
        printf("ERROR: Failed to setup I2S audio\n");
        return -1;
    }
    
    printf("I2S audio setup successful\n");
    printf("  Sample rate: %d Hz\n", output_format->sample_freq);
    printf("  Format: %d, Channels: %d\n", output_format->format, output_format->channel_count);
    
    // Create audio buffer pool for output
    audio_pool = audio_new_producer_pool(&audio_buffer_format, 3, AUDIO_BLOCK_SIZE);
    if (audio_pool == NULL) {
        printf("ERROR: Failed to create audio buffer pool\n");
        return -1;
    }
    
    // Connect audio producer to I2S output
    if (!audio_i2s_connect(audio_pool)) {
        printf("ERROR: Failed to connect audio to I2S\n");
        return -1;
    }
    
    // Enable I2S audio output
    audio_i2s_set_enabled(true);
    
    printf("Audio I2S connected and enabled\n");
    
    // TODO: Initialize Heavy context
    // heavy_context = hv_heavy_new((double)SAMPLE_RATE);
    // if (heavy_context == NULL) {
    //     printf("ERROR: Failed to create Heavy context\n");
    //     return -1;
    // }
    // printf("Heavy context created (sample rate: %d Hz)\n", SAMPLE_RATE);
    
    printf("Entering main loop...\n");
    
    // Main loop - feed audio buffers
    while (true) {
        audio_producer_task();
        // Small delay to prevent tight loop
        sleep_us(100);
    }
    
    // Cleanup (unreachable in current implementation)
    // if (heavy_context != NULL) {
    //     hv_heavy_free(heavy_context);
    // }
    
    return 0;
}
