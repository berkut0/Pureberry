/**
 * Patch API - single module owning MIDI semantics and send hook.
 *
 * Rule: hv_setSendHook() is called exactly in patch_api_init(ctx).
 * Drivers (ws2812, etc.) never call hv_setSendHook(); they only consume transport outputs.
 *
 * Command table (patch name in [s ...] -> message format -> handler):
 *   set_led_color   (r g b)       -> led mailbox (set all)
 * Future commands: use cmd_* or fw_* naming in patch; add handler + table entry here.
 */

#include "patch_api.h"
#include "crosscore_bus.h"
#include "config.h"
#include "HvHeavy.h"

#include <stdio.h>
#include <string.h>

/* --- Transport push observability: lock-free counters (core0 + core1). --- */

typedef enum {
    CTRL_SRC_USB_MIDI = 0,
    CTRL_SRC_ADC_POTS = 1,
    CTRL_SRC_MPR121 = 2,
    CTRL_SRC_GENERIC = 3,
} ctrl_push_source_t;

static patch_api_transport_stats_t g_transport_stats;

static inline void stats_inc(uint32_t *value) {
    (void) __atomic_fetch_add(value, 1u, __ATOMIC_RELAXED);
}

static inline uint32_t stats_load(const uint32_t *value) {
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static inline void stats_store(uint32_t *value, uint32_t new_value) {
    __atomic_store_n(value, new_value, __ATOMIC_RELAXED);
}

static void record_ctrl_push(bool ok, ctrl_push_source_t src) {
    if (ok) {
        stats_inc(&g_transport_stats.ctrl_queue_push_ok);
    } else {
        stats_inc(&g_transport_stats.ctrl_queue_push_drop);
    }

    switch (src) {
        case CTRL_SRC_USB_MIDI:
            if (ok) stats_inc(&g_transport_stats.ctrl_queue_push_ok_usb_midi);
            else stats_inc(&g_transport_stats.ctrl_queue_push_drop_usb_midi);
            break;
        case CTRL_SRC_ADC_POTS:
            if (ok) stats_inc(&g_transport_stats.ctrl_queue_push_ok_adc_pots);
            else stats_inc(&g_transport_stats.ctrl_queue_push_drop_adc_pots);
            break;
        case CTRL_SRC_MPR121:
            if (ok) stats_inc(&g_transport_stats.ctrl_queue_push_ok_mpr121);
            else stats_inc(&g_transport_stats.ctrl_queue_push_drop_mpr121);
            break;
        default:
            break;
    }
}

static void record_led_publish_from_send_hook(void) {
    stats_inc(&g_transport_stats.led_mailbox_publish);
    stats_inc(&g_transport_stats.led_mailbox_publish_patch_send_hook);
}

void patch_api_get_transport_stats(patch_api_transport_stats_t *out) {
    if (!out) return;
    out->ctrl_queue_push_ok = stats_load(&g_transport_stats.ctrl_queue_push_ok);
    out->ctrl_queue_push_drop = stats_load(&g_transport_stats.ctrl_queue_push_drop);
    out->led_mailbox_publish = stats_load(&g_transport_stats.led_mailbox_publish);
    out->ctrl_queue_push_ok_usb_midi = stats_load(&g_transport_stats.ctrl_queue_push_ok_usb_midi);
    out->ctrl_queue_push_drop_usb_midi = stats_load(&g_transport_stats.ctrl_queue_push_drop_usb_midi);
    out->ctrl_queue_push_ok_adc_pots = stats_load(&g_transport_stats.ctrl_queue_push_ok_adc_pots);
    out->ctrl_queue_push_drop_adc_pots = stats_load(&g_transport_stats.ctrl_queue_push_drop_adc_pots);
    out->ctrl_queue_push_ok_mpr121 = stats_load(&g_transport_stats.ctrl_queue_push_ok_mpr121);
    out->ctrl_queue_push_drop_mpr121 = stats_load(&g_transport_stats.ctrl_queue_push_drop_mpr121);
    out->led_mailbox_publish_patch_send_hook =
        stats_load(&g_transport_stats.led_mailbox_publish_patch_send_hook);
}

void patch_api_reset_transport_stats(void) {
    stats_store(&g_transport_stats.ctrl_queue_push_ok, 0u);
    stats_store(&g_transport_stats.ctrl_queue_push_drop, 0u);
    stats_store(&g_transport_stats.led_mailbox_publish, 0u);
    stats_store(&g_transport_stats.ctrl_queue_push_ok_usb_midi, 0u);
    stats_store(&g_transport_stats.ctrl_queue_push_drop_usb_midi, 0u);
    stats_store(&g_transport_stats.ctrl_queue_push_ok_adc_pots, 0u);
    stats_store(&g_transport_stats.ctrl_queue_push_drop_adc_pots, 0u);
    stats_store(&g_transport_stats.ctrl_queue_push_ok_mpr121, 0u);
    stats_store(&g_transport_stats.ctrl_queue_push_drop_mpr121, 0u);
    stats_store(&g_transport_stats.led_mailbox_publish_patch_send_hook, 0u);
}

/* --- Patch IN registry (HvParameterInfo): single source of truth for IN names/hashes. --- */

#ifndef PATCH_API_MAX_IN_PARAMS
#define PATCH_API_MAX_IN_PARAMS 64
#endif

#ifndef PATCH_API_PARAM_NAME_MAX
#define PATCH_API_PARAM_NAME_MAX 64
#endif

typedef struct {
    char name[PATCH_API_PARAM_NAME_MAX];
    uint32_t hash;
    patch_api_in_param_type_t type;
    float min_value;
    float max_value;
    float default_value;
} patch_api_in_param_slot_t;

static patch_api_in_param_slot_t g_in_params[PATCH_API_MAX_IN_PARAMS];
static uint16_t g_in_param_count;

static int find_in_param_index_by_name(const char *name) {
    if (!name || name[0] == '\0') return -1;
    for (uint16_t i = 0; i < g_in_param_count; i++) {
        if (strcmp(g_in_params[i].name, name) == 0) return (int) i;
    }
    return -1;
}

static bool find_in_param_hash_by_name(const char *name, uint32_t *out_hash) {
    int index = find_in_param_index_by_name(name);
    if (index < 0 || !out_hash) return false;
    *out_hash = g_in_params[index].hash;
    return true;
}

static int find_in_param_index_by_hash(uint32_t hash) {
    if (hash == 0u) return -1;
    for (uint16_t i = 0; i < g_in_param_count; i++) {
        if (g_in_params[i].hash == hash) return (int) i;
    }
    return -1;
}

static float clamp_float_to_param_range(const patch_api_in_param_slot_t *slot, float value) {
    if (!slot) return value;
    if (slot->type != PATCH_API_IN_PARAM_FLOAT) return value;
    if (slot->max_value < slot->min_value) return value;
    if (value < slot->min_value) return slot->min_value;
    if (value > slot->max_value) return slot->max_value;
    return value;
}

static void build_in_param_registry(HeavyContextInterface *ctx) {
    g_in_param_count = 0;
    if (!ctx) return;

    int total = hv_getParameterInfo(ctx, 0, NULL);
    if (total <= 0) return;

    for (int i = 0; i < total; i++) {
        HvParameterInfo info;
        (void) hv_getParameterInfo(ctx, i, &info);

        if (info.type != HV_PARAM_TYPE_PARAMETER_IN &&
            info.type != HV_PARAM_TYPE_EVENT_IN) {
            continue;
        }
        if (!info.name || info.name[0] == '\0' || info.hash == 0u) continue;

        int existing = find_in_param_index_by_name(info.name);
        if (existing >= 0) {
            g_in_params[existing].hash = info.hash;
            g_in_params[existing].type = (info.type == HV_PARAM_TYPE_EVENT_IN)
                ? PATCH_API_IN_PARAM_EVENT
                : PATCH_API_IN_PARAM_FLOAT;
            g_in_params[existing].min_value = info.minVal;
            g_in_params[existing].max_value = info.maxVal;
            g_in_params[existing].default_value = info.defaultVal;
            continue;
        }

        if (g_in_param_count >= (uint16_t) PATCH_API_MAX_IN_PARAMS) break;

        patch_api_in_param_slot_t *slot = &g_in_params[g_in_param_count++];
        (void) snprintf(slot->name, sizeof(slot->name), "%s", info.name);
        slot->hash = info.hash;
        slot->type = (info.type == HV_PARAM_TYPE_EVENT_IN)
            ? PATCH_API_IN_PARAM_EVENT
            : PATCH_API_IN_PARAM_FLOAT;
        slot->min_value = info.minVal;
        slot->max_value = info.maxVal;
        slot->default_value = info.defaultVal;
    }
}

uint16_t patch_api_in_param_count(void) {
    return g_in_param_count;
}

bool patch_api_get_in_param(uint16_t index, patch_api_in_param_t *out) {
    if (!out || index >= g_in_param_count) return false;
    const patch_api_in_param_slot_t *slot = &g_in_params[index];
    out->name = slot->name;
    out->hash = slot->hash;
    out->type = slot->type;
    out->min_value = slot->min_value;
    out->max_value = slot->max_value;
    out->default_value = slot->default_value;
    return true;
}

bool patch_api_find_in_param(const char *name, patch_api_in_param_t *out) {
    if (!name || !out) return false;
    int index = find_in_param_index_by_name(name);
    if (index < 0) return false;
    return patch_api_get_in_param((uint16_t) index, out);
}

bool patch_api_push_in_float_by_name(const char *name, float value) {
    int index = find_in_param_index_by_name(name);
    if (index < 0) return false;
    const patch_api_in_param_slot_t *slot = &g_in_params[index];
    if (slot->type != PATCH_API_IN_PARAM_FLOAT) return false;

    float clamped = clamp_float_to_param_range(slot, value);
    bool ok = crosscore_bus_ctrl_try_push_f(slot->hash, clamped);
    record_ctrl_push(ok, CTRL_SRC_GENERIC);
    return ok;
}

bool patch_api_push_in_float_by_hash(uint32_t hash, float value) {
    int index = find_in_param_index_by_hash(hash);
    if (index < 0) return false;
    const patch_api_in_param_slot_t *slot = &g_in_params[index];
    if (slot->type != PATCH_API_IN_PARAM_FLOAT) return false;

    float clamped = clamp_float_to_param_range(slot, value);
    bool ok = crosscore_bus_ctrl_try_push_f(slot->hash, clamped);
    record_ctrl_push(ok, CTRL_SRC_GENERIC);
    return ok;
}

bool patch_api_push_in_bang_by_name(const char *name) {
    int index = find_in_param_index_by_name(name);
    if (index < 0) return false;
    const patch_api_in_param_slot_t *slot = &g_in_params[index];
    if (slot->type != PATCH_API_IN_PARAM_EVENT) return false;

    bool ok = crosscore_bus_ctrl_try_push_b(slot->hash);
    record_ctrl_push(ok, CTRL_SRC_GENERIC);
    return ok;
}

bool patch_api_push_in_bang_by_hash(uint32_t hash) {
    int index = find_in_param_index_by_hash(hash);
    if (index < 0) return false;
    const patch_api_in_param_slot_t *slot = &g_in_params[index];
    if (slot->type != PATCH_API_IN_PARAM_EVENT) return false;

    bool ok = crosscore_bus_ctrl_try_push_b(slot->hash);
    record_ctrl_push(ok, CTRL_SRC_GENERIC);
    return ok;
}

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
    bool ok = crosscore_bus_ctrl_try_push_fff(hash_notein, (float)note, (float)vel, (float)ch);
    record_ctrl_push(ok, CTRL_SRC_USB_MIDI);
    return ok;
}

bool patch_api_push_ctlin(uint8_t ctrl, uint8_t val, uint8_t ch) {
    ensure_midi_hashes();
    /* Canonical order: (value, cc, channel0) */
    bool ok = crosscore_bus_ctrl_try_push_fff(hash_ctlin, (float)val, (float)ctrl, (float)ch);
    record_ctrl_push(ok, CTRL_SRC_USB_MIDI);
    return ok;
}

bool patch_api_push_bendin(int16_t bend, uint8_t ch) {
    ensure_midi_hashes();
    bool ok = crosscore_bus_ctrl_try_push_ff(hash_bendin, (float)bend, (float)ch);
    record_ctrl_push(ok, CTRL_SRC_USB_MIDI);
    return ok;
}

bool patch_api_push_pgmin(uint8_t prog, uint8_t ch) {
    ensure_midi_hashes();
    bool ok = crosscore_bus_ctrl_try_push_ff(hash_pgmin, (float)prog, (float)ch);
    record_ctrl_push(ok, CTRL_SRC_USB_MIDI);
    return ok;
}

bool patch_api_push_touchin(uint8_t pressure, uint8_t ch) {
    ensure_midi_hashes();
    bool ok = crosscore_bus_ctrl_try_push_ff(hash_touchin, (float)pressure, (float)ch);
    record_ctrl_push(ok, CTRL_SRC_USB_MIDI);
    return ok;
}

bool patch_api_push_polytouchin(uint8_t note, uint8_t pressure, uint8_t ch) {
    ensure_midi_hashes();
    /* Canonical order: (pressure, note, channel0) */
    bool ok = crosscore_bus_ctrl_try_push_fff(hash_polytouchin, (float)pressure, (float)note, (float)ch);
    record_ctrl_push(ok, CTRL_SRC_USB_MIDI);
    return ok;
}

/* --- Knob (Daisy-style knob1..knob4) hashes; push by index. ADC driver does not know names. --- */

#define PATCH_API_KNOB_NAME_BUF 16
static uint32_t hash_knob[POTS_MAX];
static bool knob_hashes_done;

static void ensure_knob_hashes(void) {
    if (knob_hashes_done) return;
    for (uint8_t i = 0; i < (uint8_t) POTS_MAX; i++) {
        char name[PATCH_API_KNOB_NAME_BUF];
        (void) snprintf(name, sizeof(name), "knob%u", (unsigned) (i + 1u));
        hash_knob[i] = (uint32_t) hv_stringToHash(name);

        uint32_t resolved_hash = 0;
        if (find_in_param_hash_by_name(name, &resolved_hash)) {
            hash_knob[i] = resolved_hash;
        }
    }
    knob_hashes_done = true;
}

bool patch_api_push_knob(uint8_t index, float value) {
    ensure_knob_hashes();
    if (index >= (unsigned) POTS_COUNT || index >= (unsigned) POTS_MAX) return false;
    bool ok = crosscore_bus_ctrl_try_push_f(hash_knob[index], value);
    record_ctrl_push(ok, CTRL_SRC_ADC_POTS);
    return ok;
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
        uint32_t resolved_hash = 0;
        if (find_in_param_hash_by_name(name, &resolved_hash)) {
            hash_touch[i] = resolved_hash;
        }

        (void) snprintf(name, sizeof(name), "touch%u_level", (unsigned) (i + 1u));
        hash_touch_level[i] = (uint32_t) hv_stringToHash(name);
        if (find_in_param_hash_by_name(name, &resolved_hash)) {
            hash_touch_level[i] = resolved_hash;
        }
    }
    touch_hashes_done = true;
}
#endif

bool patch_api_push_touch(uint8_t index, bool touched) {
#ifdef ENABLE_MPR121
    ensure_touch_hashes();
    if (index >= (uint8_t) MPR121_NUM_ELECTRODES) return false;
    bool ok = crosscore_bus_ctrl_try_push_f(hash_touch[index], touched ? 1.0f : 0.0f);
    record_ctrl_push(ok, CTRL_SRC_MPR121);
    return ok;
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
    bool ok = crosscore_bus_ctrl_try_push_f(hash_touch_level[index], level);
    record_ctrl_push(ok, CTRL_SRC_MPR121);
    return ok;
#else
    (void) index;
    (void) level;
    return false;
#endif
}

/* --- Send hook: single entry point; route by sendHash to handlers that parse and publish. --- */

typedef void (*send_cmd_handler_t)(const HvMessage *msg);

typedef struct {
    const char *send_name;
    hv_uint32_t send_hash;
    uint8_t min_arity;
    send_cmd_handler_t handler;
} send_cmd_route_t;

#ifdef ENABLE_WS2812
static uint32_t parse_rgb_0_255(const HvMessage *msg, int first_index) {
    float r = hv_msg_getFloat(msg, first_index + 0);
    float g = hv_msg_getFloat(msg, first_index + 1);
    float b = hv_msg_getFloat(msg, first_index + 2);
    const float scale = 255.0f;

    if (r <= 1.0f && g <= 1.0f && b <= 1.0f) {
        r *= scale;
        g *= scale;
        b *= scale;
    }

    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;

    return ((uint32_t)(uint8_t)r << 16) | ((uint32_t)(uint8_t)g << 8) | ((uint32_t)(uint8_t)b);
}

static void handle_set_led_color(const HvMessage *msg) {
    uint32_t rgb = parse_rgb_0_255(msg, 0);
    crosscore_bus_led_publish_color(rgb);
    record_led_publish_from_send_hook();
}
#else
static void handle_set_led_color(const HvMessage *msg) {
    (void) msg;
}
#endif

static send_cmd_route_t send_routes[] = {
    {
        .send_name = "set_led_color",
        .send_hash = 0,
        .min_arity = 3,
        .handler = handle_set_led_color
    }
};

static bool cmd_hashes_done;

static void ensure_cmd_hashes(void) {
    if (cmd_hashes_done) return;
    for (unsigned i = 0; i < (unsigned)(sizeof(send_routes) / sizeof(send_routes[0])); i++) {
        send_routes[i].send_hash = hv_stringToHash(send_routes[i].send_name);
    }
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
    int argc = hv_msg_getNumElements(msg);
    for (unsigned i = 0; i < (unsigned)(sizeof(send_routes) / sizeof(send_routes[0])); i++) {
        if (sendHash != send_routes[i].send_hash) continue;
        if (argc < (int) send_routes[i].min_arity) return;
        send_routes[i].handler(msg);
        return;
    }
}

void patch_api_init(struct HeavyContextInterface *ctx) {
    if (!ctx) return;
    build_in_param_registry(ctx);

    /* Re-resolve IN hashes against active patch metadata. */
    knob_hashes_done = false;
#ifdef ENABLE_MPR121
    touch_hashes_done = false;
#endif

    /* Single entry point: only here we call hv_setSendHook. No driver must ever call it. */
    hv_setSendHook(ctx, patch_send_hook);
}

