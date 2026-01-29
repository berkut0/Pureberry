/**
 * Multicore audio: transport only — queues, hv_event_t, drain, ctrl_push_hash_*.
 * Core0 pushes control events to ctrl_queue via ctrl_push_hash_* (or patch_api MIDI helpers);
 * core1 drains ctrl_queue and applies to Heavy.
 * Patch API send hook (patch_api.c) pushes LED commands to led_queue; core0 drains and calls ws2812_*.
 */

#ifndef MULTICORE_AUDIO_H
#define MULTICORE_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/util/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration so callers need not include HvHeavy.h */
struct HeavyContextInterface;

/** Control event: core0 -> core1 (MIDI etc.). Applied by core1 via hv_sendMessageToReceiver*. */
typedef struct {
    uint32_t receiver_hash;
    uint8_t argc;  /* 0..3 */
    float argv[3];
} hv_event_t;

/** LED command: core1 send hook -> core0. Drained by core0, calls ws2812_set_* */
typedef enum {
    LED_CMD_SET_COLOR,   /* set all: rgb only */
    LED_CMD_SET_INDEX    /* set one: index + rgb */
} led_cmd_type_t;

typedef struct {
    led_cmd_type_t type;
    int index;       /* for SET_INDEX; -1 for SET_COLOR */
    uint32_t rgb;    /* 0xRRGGBB */
} led_cmd_t;

/** Queues (defined in multicore_audio.c); init before launching core1 */
extern queue_t ctrl_queue;
extern queue_t led_queue;

/** Init queues; call from core0 after audio_pool creation, before hv_patch_new */
void multicore_audio_init(void);

/** Push from core0 by receiver hash. On overflow: drop newest. */
bool ctrl_push_hash_f(uint32_t receiver_hash, float a);
bool ctrl_push_hash_ff(uint32_t receiver_hash, float a, float b);
bool ctrl_push_hash_fff(uint32_t receiver_hash, float a, float b, float c);

/** Drain on core1 before each audio buffer; applies events to Heavy */
void multicore_drain_ctrl(struct HeavyContextInterface *hv);

/** Drain on core0 in main loop; applies LED commands via ws2812_set_* */
void multicore_drain_led(void);

#ifdef __cplusplus
}
#endif

#endif /* MULTICORE_AUDIO_H */
