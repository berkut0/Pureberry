/**
 * Cross-core transport bus.
 *
 * Policy: ctrl queue overflow = drop newest. LED path uses latest-wins mailbox
 * (single-writer core1 / single-reader core0) with sequence protocol.
 */

#include "crosscore_bus.h"

#include "HvHeavy.h"
#include "pico/util/queue.h"

typedef struct {
    uint32_t receiver_hash;
    uint8_t argc;  /* 0..3 */
    float argv[3];
} crosscore_ctrl_event_t;

#define CROSSCORE_CTRL_QUEUE_LEN 64

static queue_t g_ctrl_queue;

typedef struct {
    uint32_t seq;
    uint32_t rgb;
} crosscore_led_mailbox_t;

static crosscore_led_mailbox_t g_led_mb;
static uint32_t g_led_last_seq_consumed;

static void led_publish(uint32_t rgb) {
    uint32_t s = __atomic_load_n(&g_led_mb.seq, __ATOMIC_RELAXED);
    __atomic_store_n(&g_led_mb.seq, s + 1u, __ATOMIC_RELEASE); /* odd = writer active */
    __atomic_store_n(&g_led_mb.rgb, rgb, __ATOMIC_RELAXED);
    __atomic_store_n(&g_led_mb.seq, s + 2u, __ATOMIC_RELEASE); /* even = committed */
}

void crosscore_bus_init(void) {
    queue_init(&g_ctrl_queue, sizeof(crosscore_ctrl_event_t), CROSSCORE_CTRL_QUEUE_LEN);
    __atomic_store_n(&g_led_mb.seq, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_led_mb.rgb, 0u, __ATOMIC_RELAXED);
    g_led_last_seq_consumed = 0u;
}

bool crosscore_bus_ctrl_try_push_f(uint32_t receiver_hash, float a) {
    crosscore_ctrl_event_t ev = {
        .receiver_hash = receiver_hash,
        .argc = 1,
        .argv = {a, 0.0f, 0.0f}
    };
    return queue_try_add(&g_ctrl_queue, &ev);
}

bool crosscore_bus_ctrl_try_push_ff(uint32_t receiver_hash, float a, float b) {
    crosscore_ctrl_event_t ev = {
        .receiver_hash = receiver_hash,
        .argc = 2,
        .argv = {a, b, 0.0f}
    };
    return queue_try_add(&g_ctrl_queue, &ev);
}

bool crosscore_bus_ctrl_try_push_fff(uint32_t receiver_hash, float a, float b, float c) {
    crosscore_ctrl_event_t ev = {
        .receiver_hash = receiver_hash,
        .argc = 3,
        .argv = {a, b, c}
    };
    return queue_try_add(&g_ctrl_queue, &ev);
}

void crosscore_bus_ctrl_drain_to_heavy(struct HeavyContextInterface *hv) {
    if (!hv) return;
    HeavyContextInterface *h = (HeavyContextInterface *) hv;
    crosscore_ctrl_event_t ev;
    while (queue_try_remove(&g_ctrl_queue, &ev)) {
        switch (ev.argc) {
            case 0:
                (void) hv_sendMessageToReceiverV(h, (hv_uint32_t) ev.receiver_hash, 0.0, "b");
                break;
            case 1:
                (void) hv_sendMessageToReceiverV(h, (hv_uint32_t) ev.receiver_hash, 0.0, "f", (double) ev.argv[0]);
                break;
            case 2:
                (void) hv_sendMessageToReceiverFF(h, (hv_uint32_t) ev.receiver_hash, 0.0,
                                                 (double) ev.argv[0], (double) ev.argv[1]);
                break;
            case 3:
                (void) hv_sendMessageToReceiverFFF(h, (hv_uint32_t) ev.receiver_hash, 0.0,
                                                  (double) ev.argv[0], (double) ev.argv[1], (double) ev.argv[2]);
                break;
            default:
                break;
        }
    }
}

void crosscore_bus_led_publish_color(uint32_t rgb) {
    led_publish(rgb);
}

bool crosscore_bus_led_try_consume_latest(crosscore_led_update_t *out) {
    if (!out) return false;
    uint32_t seq_begin;
    uint32_t seq_end;
    crosscore_led_update_t tmp;
    do {
        seq_begin = __atomic_load_n(&g_led_mb.seq, __ATOMIC_ACQUIRE);
        if (seq_begin == 0u || (seq_begin & 1u)) return false;
        tmp.rgb = __atomic_load_n(&g_led_mb.rgb, __ATOMIC_RELAXED);
        seq_end = __atomic_load_n(&g_led_mb.seq, __ATOMIC_ACQUIRE);
    } while (seq_end != seq_begin || (seq_end & 1u));

    if (seq_end == g_led_last_seq_consumed) return false;
    g_led_last_seq_consumed = seq_end;
    *out = tmp;
    return true;
}
