/**
 * Patch API — single module owning MIDI semantics and send hook.
 *
 * Rule: hv_setSendHook() is called exactly in patch_api_init(ctx).
 * Drivers (ws2812, etc.) never call hv_setSendHook(); they only consume from queues.
 *
 * Command table (patch name in [s ...] -> message format -> queue/handler):
 *   set_led_color   (r g b)       -> led_queue (LED_CMD_SET_COLOR)
 *   set_led_index   (idx r g b)   -> led_queue (LED_CMD_SET_INDEX)
 * Future commands: use cmd_* or fw_* naming in patch; add hash + branch here.
 */

#include "patch_api.h"
#include "multicore_audio.h"
#include "config.h"
#include "HvHeavy.h"

#ifdef ENABLE_WS2812
#include "dev/ws2812.h"
#endif

#include <stdio.h>

/* --- MIDI: __hv_* hashes and push helpers (canonical argv order per hvcc test_midi.cpp) --- */

static uint32_t hash_notein;
static uint32_t hash_ctlin;
static uint32_t hash_bendin;
static uint32_t hash_pgmin;
static uint32_t hash_touchin;
static uint32_t hash_polytouchin;
static bool midi_hashes_done;

static void ensure_midi_hashes(void) {
    if (midi_hashes_done) return;
    hash_notein     = (uint32_t) hv_stringToHash("__hv_notein");
    hash_ctlin      = (uint32_t) hv_stringToHash("__hv_ctlin");
    hash_bendin     = (uint32_t) hv_stringToHash("__hv_bendin");
    hash_pgmin      = (uint32_t) hv_stringToHash("__hv_pgmin");
    hash_touchin    = (uint32_t) hv_stringToHash("__hv_touchin");
    hash_polytouchin = (uint32_t) hv_stringToHash("__hv_polytouchin");
    midi_hashes_done = true;
}

bool patch_api_push_notein(uint8_t note, uint8_t vel, uint8_t ch) {
    ensure_midi_hashes();
    return ctrl_push_hash_fff(hash_notein, (float)note, (float)vel, (float)ch);
}

bool patch_api_push_ctlin(uint8_t ctrl, uint8_t val, uint8_t ch) {
    ensure_midi_hashes();
    /* Canonical order: (value, cc, channel0) */
    return ctrl_push_hash_fff(hash_ctlin, (float)val, (float)ctrl, (float)ch);
}

bool patch_api_push_bendin(int16_t bend, uint8_t ch) {
    ensure_midi_hashes();
    return ctrl_push_hash_ff(hash_bendin, (float)bend, (float)ch);
}

bool patch_api_push_pgmin(uint8_t prog, uint8_t ch) {
    ensure_midi_hashes();
    return ctrl_push_hash_ff(hash_pgmin, (float)prog, (float)ch);
}

bool patch_api_push_touchin(uint8_t pressure, uint8_t ch) {
    ensure_midi_hashes();
    return ctrl_push_hash_ff(hash_touchin, (float)pressure, (float)ch);
}

bool patch_api_push_polytouchin(uint8_t note, uint8_t pressure, uint8_t ch) {
    ensure_midi_hashes();
    /* Canonical order: (pressure, note, channel0) */
    return ctrl_push_hash_fff(hash_polytouchin, (float)pressure, (float)note, (float)ch);
}

/* --- Knob (Daisy-style knob1..knob4) hashes; push by index. ADC driver does not know names. --- */

static uint32_t hash_knob[POTS_MAX];
static bool knob_hashes_done;

static void ensure_knob_hashes(void) {
    if (knob_hashes_done) return;
    hash_knob[0] = (uint32_t) hv_stringToHash("knob1");
    hash_knob[1] = (uint32_t) hv_stringToHash("knob2");
    hash_knob[2] = (uint32_t) hv_stringToHash("knob3");
    hash_knob[3] = (uint32_t) hv_stringToHash("knob4");
    knob_hashes_done = true;
}

bool patch_api_push_knob(uint8_t index, float value) {
    ensure_knob_hashes();
    if (index >= (unsigned) POTS_COUNT || index >= (unsigned) POTS_MAX) return false;
    return ctrl_push_hash_f(hash_knob[index], value);
}

/* --- Touch (touch1..touch12 + touchN_level) hashes; push by index from MPR121 driver. --- */

#ifdef ENABLE_MPR121
#define PATCH_API_TOUCH_NAME_BUF 24
static uint32_t hash_touch[MPR121_NUM_ELECTRODES];
static uint32_t hash_touch_level[MPR121_NUM_ELECTRODES];
static bool touch_hashes_done;

static void ensure_touch_hashes(void) {
    if (touch_hashes_done) return;
    for (uint8_t i = 0; i < (uint8_t) MPR121_NUM_ELECTRODES; i++) {
        char name[PATCH_API_TOUCH_NAME_BUF];
        (void) snprintf(name, sizeof(name), "touch%u", (unsigned) (i + 1u));
        hash_touch[i] = (uint32_t) hv_stringToHash(name);
        (void) snprintf(name, sizeof(name), "touch%u_level", (unsigned) (i + 1u));
        hash_touch_level[i] = (uint32_t) hv_stringToHash(name);
    }
    touch_hashes_done = true;
}
#endif

bool patch_api_push_touch(uint8_t index, bool touched) {
#ifdef ENABLE_MPR121
    ensure_touch_hashes();
    if (index >= (uint8_t) MPR121_NUM_ELECTRODES) return false;
    return ctrl_push_hash_f(hash_touch[index], touched ? 1.0f : 0.0f);
#else
    (void) index;
    (void) touched;
    return false;
#endif
}

bool patch_api_push_touch_level(uint8_t index, float level) {
#ifdef ENABLE_MPR121
    ensure_touch_hashes();
    if (index >= (uint8_t) MPR121_NUM_ELECTRODES) return false;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    return ctrl_push_hash_f(hash_touch_level[index], level);
#else
    (void) index;
    (void) level;
    return false;
#endif
}

/* --- Send hook: single entry point; route by sendHash to handlers that only parse and enqueue --- */

static hv_uint32_t hash_set_led_color;
static hv_uint32_t hash_set_led_index;
static bool cmd_hashes_done;

static void ensure_cmd_hashes(void) {
    if (cmd_hashes_done) return;
    hash_set_led_color = hv_stringToHash("set_led_color");
    hash_set_led_index = hv_stringToHash("set_led_index");
    cmd_hashes_done = true;
}

static void patch_send_hook(HeavyContextInterface *context,
                            const char *sendName,
                            hv_uint32_t sendHash,
                            const HvMessage *msg) {
    (void) context;
    (void) sendName;
    if (!msg) return;

    ensure_cmd_hashes();
    const float scale = 255.0f;

#ifdef ENABLE_WS2812
    if (sendHash == hash_set_led_color && hv_msg_getNumElements(msg) >= 3) {
        float r = hv_msg_getFloat(msg, 0);
        float g = hv_msg_getFloat(msg, 1);
        float b = hv_msg_getFloat(msg, 2);
        if (r <= 1.0f && g <= 1.0f && b <= 1.0f) { r *= scale; g *= scale; b *= scale; }
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        uint32_t rgb = ((uint32_t)(uint8_t)r << 16) | ((uint32_t)(uint8_t)g << 8) | ((uint32_t)(uint8_t)b);
        led_cmd_t cmd = { .type = LED_CMD_SET_COLOR, .index = -1, .rgb = rgb };
        (void) queue_try_add(&led_queue, &cmd);
        return;
    }

    if (sendHash == hash_set_led_index && hv_msg_getNumElements(msg) >= 4) {
        const int idx = (int) hv_msg_getFloat(msg, 0);
        float r = hv_msg_getFloat(msg, 1);
        float g = hv_msg_getFloat(msg, 2);
        float b = hv_msg_getFloat(msg, 3);
        if (r <= 1.0f && g <= 1.0f && b <= 1.0f) { r *= scale; g *= scale; b *= scale; }
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        uint32_t rgb = ((uint32_t)(uint8_t)r << 16) | ((uint32_t)(uint8_t)g << 8) | ((uint32_t)(uint8_t)b);
        if (idx >= 0 && (uint) idx < ws2812_get_num_leds()) {
            led_cmd_t cmd = { .type = LED_CMD_SET_INDEX, .index = idx, .rgb = rgb };
            (void) queue_try_add(&led_queue, &cmd);
        }
        return;
    }
#else
    (void) scale;
#endif
}

void patch_api_init(struct HeavyContextInterface *ctx) {
    if (!ctx) return;
    /* Single entry point: only here we call hv_setSendHook. No driver must ever call it. */
    hv_setSendHook(ctx, patch_send_hook);
}
