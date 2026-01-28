/**
 * Multicore audio: queue init, ctrl_push_*, drain_ctrl, drain_led.
 * Policy: ctrl_queue overflow = drop newest (queue_try_add returns false, we drop).
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

/* Receiver hashes (Heavy Pd receivers); computed once at init */
static uint32_t hash_notein;
static uint32_t hash_ctlin;
static uint32_t hash_bendin;
static uint32_t hash_pgmin;
static uint32_t hash_touchin;
static uint32_t hash_polytouchin;
static bool hashes_done;

static void ensure_hashes(void) {
    if (hashes_done) return;
    hash_notein     = (uint32_t) hv_stringToHash("notein");
    hash_ctlin      = (uint32_t) hv_stringToHash("ctlin");
    hash_bendin     = (uint32_t) hv_stringToHash("bendin");
    hash_pgmin      = (uint32_t) hv_stringToHash("pgmin");
    hash_touchin    = (uint32_t) hv_stringToHash("touchin");
    hash_polytouchin = (uint32_t) hv_stringToHash("polytouchin");
    hashes_done = true;
}

void multicore_audio_init(void) {
    ensure_hashes();
    queue_init(&ctrl_queue, sizeof(hv_event_t), CTRL_QUEUE_LEN);
    queue_init(&led_queue, sizeof(led_cmd_t), LED_QUEUE_LEN);
}

/* ctrl_push_*: fill hv_event_t and queue_try_add; on overflow drop (return false) */
bool ctrl_push_notein(uint8_t note, uint8_t vel, uint8_t ch) {
    hv_event_t ev = {
        .receiver_hash = hash_notein,
        .argc = 3,
        .argv = { (float)note, (float)vel, (float)ch }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

bool ctrl_push_ctlin(uint8_t ctrl, uint8_t val, uint8_t ch) {
    hv_event_t ev = {
        .receiver_hash = hash_ctlin,
        .argc = 3,
        .argv = { (float)ctrl, (float)val, (float)ch }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

bool ctrl_push_bendin(int16_t bend, uint8_t ch) {
    hv_event_t ev = {
        .receiver_hash = hash_bendin,
        .argc = 2,
        .argv = { (float)bend, (float)ch, 0.0f }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

bool ctrl_push_pgmin(uint8_t prog, uint8_t ch) {
    hv_event_t ev = {
        .receiver_hash = hash_pgmin,
        .argc = 2,
        .argv = { (float)prog, (float)ch, 0.0f }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

bool ctrl_push_touchin(uint8_t pressure, uint8_t ch) {
    hv_event_t ev = {
        .receiver_hash = hash_touchin,
        .argc = 2,
        .argv = { (float)pressure, (float)ch, 0.0f }
    };
    return queue_try_add(&ctrl_queue, &ev);
}

bool ctrl_push_polytouchin(uint8_t note, uint8_t pressure, uint8_t ch) {
    hv_event_t ev = {
        .receiver_hash = hash_polytouchin,
        .argc = 3,
        .argv = { (float)note, (float)pressure, (float)ch }
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
