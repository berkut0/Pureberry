#ifndef MULTICORE_DISPLAY_H
#define MULTICORE_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Multicore display: a small, real-time safe bridge from core1 (audio) to core0 (I/O).
 *
 * core1 publishes a 128-point waveform (already mapped to display Y coordinates).
 * core0 reads the latest published waveform when drawing the OLED.
 */

#define MULTICORE_WAVEFORM_WIDTH 128

/** Capture from an interleaved (L,R) float buffer produced by Heavy (core1 only). */
void multicore_display_capture_interleaved(const float *lr_interleaved, size_t frames);

/** Read the latest waveform (core0 only). Returns false if nothing published yet. */
bool multicore_display_read_latest(uint8_t out_y[MULTICORE_WAVEFORM_WIDTH]);

#ifdef __cplusplus
}
#endif

#endif /* MULTICORE_DISPLAY_H */
