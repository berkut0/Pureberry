/**
 * Audio runtime (I2S output + core1 DSP loop).
 *
 * Owns audio device setup, core1 render loop and float->PCM conversion.
 * Control event transport stays in crosscore_bus.* (queues/drain only).
 */

#ifndef AUDIO_RUNTIME_H
#define AUDIO_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct HeavyContextInterface;

/** Heavy sample rate used by the runtime and patch context. */
uint32_t audio_runtime_sample_rate(void);

/**
 * Initialize I2S output pipeline.
 * Returns false if setup failed; caller may still continue running for diagnostics.
 */
bool audio_runtime_init_output(void);

/**
 * Start core1 audio loop.
 * The loop drains control queue and renders blocks via Heavy.
 */
void audio_runtime_start(struct HeavyContextInterface *ctx);

/**
 * Core1 DSP average load in permille (1000 = 100.0%).
 * Updated from the core1 loop using a ~1 second audio window.
 */
uint32_t audio_runtime_get_core1_dsp_load_avg_permille(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_RUNTIME_H */
