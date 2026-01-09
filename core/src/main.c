/**
 * RP2350 Pure Data Firmware
 * 
 * Main firmware entry point for running Pure Data patches compiled with hvcc.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sync.h"

// Heavy context interface
// These will be included from the generated hvcc code
// #include "Heavy_heavy.h"
// #include "HvHeavy.h"

// Audio configuration
#define SAMPLE_RATE 48000.0f
#define AUDIO_BLOCK_SIZE 64  // Heavy default block size
#define AUDIO_BUFFER_SIZE (AUDIO_BLOCK_SIZE * 2)  // Stereo

// Audio buffers
static float audio_in_buffer[AUDIO_BUFFER_SIZE];
static float audio_out_buffer[AUDIO_BUFFER_SIZE];

// Heavy context (will be initialized from generated code)
// static HeavyContextInterface *heavy_context = NULL;

int main() {
    // Initialize stdio (USB serial)
    stdio_init_all();
    
    printf("\n=== RP2350 Pure Data Firmware ===\n");
    printf("Initializing...\n");
    
    // TODO: Initialize I2S for audio I/O
    // This will be implemented in a later stage
    printf("I2S initialization: TODO\n");
    
    // TODO: Initialize Heavy context
    // heavy_context = hv_heavy_new(SAMPLE_RATE);
    // if (heavy_context == NULL) {
    //     printf("ERROR: Failed to create Heavy context\n");
    //     return -1;
    // }
    // printf("Heavy context created (sample rate: %.1f Hz)\n", SAMPLE_RATE);
    
    printf("Entering main loop...\n");
    
    // Main audio processing loop
    while (true) {
        // TODO: Read audio from I2S into audio_in_buffer
        
        // TODO: Process audio through Heavy
        // hv_process(heavy_context, audio_in_buffer, audio_out_buffer, AUDIO_BLOCK_SIZE);
        
        // TODO: Write audio from audio_out_buffer to I2S
        
        // Small delay to prevent tight loop
        sleep_us(100);
    }
    
    // Cleanup (unreachable in current implementation)
    // if (heavy_context != NULL) {
    //     hv_heavy_free(heavy_context);
    // }
    
    return 0;
}
