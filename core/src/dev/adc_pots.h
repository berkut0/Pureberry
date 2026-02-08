/**
 * ADC potentiometers driver (knob1..knob4).
 * Config: first channel POTS_ADC_FIRST_CHANNEL, count POTS_COUNT; pins from SDK (ADC_BASE_PIN + channel).
 * Normalizes to 0..1. No hv_setSendHook; this module pushes knob values via patch_api.
 */

#ifndef ADC_POTS_H
#define ADC_POTS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize ADC and GPIO for potentiometer channels.
 * Only valid when POTS_BACKEND == POTS_BACKEND_ADC.
 * Uses POTS_ADC_FIRST_CHANNEL and POTS_COUNT from config.
 * Returns false if config invalid or init failed.
 */
bool adc_pots_init(void);

/**
 * Poll potentiometers and push changed values to knob1..knob4 via patch_api.
 * Applies POTS_POLL_MS interval, POTS_EPS deadband and retry-on-queue-overflow behavior.
 * Safe to call each main-loop iteration.
 */
void adc_pots_task(void);

/**
 * Read pot channels: apply 1-pole filter and write normalized [0, 1] to out.
 * n must be <= POTS_COUNT.
 */
void adc_pots_read(float *out, unsigned n);

#ifdef __cplusplus
}
#endif

#endif /* ADC_POTS_H */
