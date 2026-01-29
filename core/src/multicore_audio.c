/**
 * Multicore audio: transport only — queues, hv_event_t, drain, ctrl_push_hash_*.
 * Policy: ctrl_queue overflow = drop newest (queue_try_add returns false, we drop).
 * Patch API semantics (MIDI __hv_*, commands) live in patch_api.c.
 */

#include "multicore_audio.h"
#include "HvHeavy.h"
#include "config.h"

#ifdef ENABLE_WS2812
#include "dev/ws2812.h"
#endif

#include <string.h>

#define CTRL_QUEUE_LEN 64
#define LED_QUEUE_LEN  32

queue_t ctrl_queue;
queue_t led_queue;

void multicore_audio_init(void) {
    queue_init(&ctrl_queue, sizeof(hv_event_t), CTRL_QUEUE_LEN);
    queue_init(&led_queue, sizeof(led_cmd_t), LED_QUEUE_LEN);
}

bool ctrl_push_hash_f(uint32_t receiver_hash, float a) {
    hv_event_t ev = {
        .receiver_hash = receiver_hash,
        .argc = 1,
        .argv = { a, 0.0f, 0.0f }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

bool ctrl_push_hash_ff(uint32_t receiver_hash, float a, float b) {
    hv_event_t ev = {
        .receiver_hash = receiver_hash,
        .argc = 2,
        .argv = { a, b, 0.0f }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

bool ctrl_push_hash_fff(uint32_t receiver_hash, float a, float b, float c) {
    hv_event_t ev = {
        .receiver_hash = receiver_hash,
        .argc = 3,
        .argv = { a, b, c }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

void multicore_drain_ctrl(struct HeavyContextInterface *hv) {
    if (!hv) return;
    HeavyContextInterface *h = (HeavyContextInterface *) hv;
    hv_event_t ev;
    while (queue_try_remove(&ctrl_queue, &ev)) {
        switch (ev.argc) {
            case 0:
                hv_sendMessageToReceiverV(h, (hv_uint32_t) ev.receiver_hash, 0.0, "b");
                break;
            case 1:
                hv_sendMessageToReceiverV(h, (hv_uint32_t) ev.receiver_hash, 0.0, "f", (double) ev.argv[0]);
                break;
            case 2:
                hv_sendMessageToReceiverFF(h, (hv_uint32_t) ev.receiver_hash, 0.0, (double) ev.argv[0], (double) ev.argv[1]);
                break;
            case 3:
                hv_sendMessageToReceiverFFF(h, (hv_uint32_t) ev.receiver_hash, 0.0, (double) ev.argv[0], (double) ev.argv[1], (double) ev.argv[2]);
                break;
            default:
                break;
        }
    }
}

void multicore_drain_led(void) {
#ifdef ENABLE_WS2812
    led_cmd_t cmd;
    while (queue_try_remove(&led_queue, &cmd)) {
        if (cmd.type == LED_CMD_SET_COLOR) {
            ws2812_set_all(cmd.rgb);
        } else if (cmd.type == LED_CMD_SET_INDEX && cmd.index >= 0) {
            ws2812_set_color((uint) cmd.index, cmd.rgb);
            if (ws2812_get_num_leds() > 1) {
                ws2812_update();
            }
        }
    }
#else
    (void)0;
#endif
}
