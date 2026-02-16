/**
 * Cross-core transport bus.
 *
 * Responsibilities:
 * - core0 -> core1 control event transport to Heavy receivers
 * - core1 -> core0 LED color transport from patch send hook
 *
 * Non-responsibilities:
 * - No patch semantics (MIDI naming, command routing)
 * - No hardware side-effects (handled by device modules)
 */

#ifndef CROSSCORE_BUS_H
#define CROSSCORE_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration so callers need not include HvHeavy.h */
struct HeavyContextInterface;

/** LED update payload (consumer copy). */
typedef struct {
    uint32_t rgb;
} crosscore_led_update_t;

/** Init internal queues and mailbox; call from core0 before starting core1 audio loop. */
void crosscore_bus_init(void);

/** Push control events from core0 by Heavy receiver hash. On overflow: drop newest. */
bool crosscore_bus_ctrl_try_push_b(uint32_t receiver_hash);
bool crosscore_bus_ctrl_try_push_f(uint32_t receiver_hash, float a);
bool crosscore_bus_ctrl_try_push_ff(uint32_t receiver_hash, float a, float b);
bool crosscore_bus_ctrl_try_push_fff(uint32_t receiver_hash, float a, float b, float c);

/** Drain control events on core1 before each audio buffer; applies to Heavy. */
void crosscore_bus_ctrl_drain_to_heavy(struct HeavyContextInterface *hv);

/**
 * Drain at most `max_events` control events on core1 before each audio buffer.
 * Returns number of drained events.
 */
size_t crosscore_bus_ctrl_drain_to_heavy_budgeted(struct HeavyContextInterface *hv, size_t max_events);

/** Publish LED color from core1 send hook. Latest-wins mailbox. */
void crosscore_bus_led_publish_color(uint32_t rgb);

/** Consume latest LED update on core0. Returns false when no new update. */
bool crosscore_bus_led_try_consume_latest(crosscore_led_update_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CROSSCORE_BUS_H */
