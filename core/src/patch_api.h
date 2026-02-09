/**
 * Patch API - contract between Pd patch and firmware.
 *
 * - MIDI: push to __hv_* receivers with canonical argument order (see hvcc test_midi.cpp).
 * - Patch IN registry: hvcc-exposed input params/events are introspected via HvParameterInfo.
 * - Send hook: hv_setSendHook() is called exactly once from patch_api_init(ctx).
 *   No driver (ws2812, future i2c/encoders/display) must ever call hv_setSendHook().
 * - Command table: patch name -> message format -> handler (e.g. set_led_color).
 */

#ifndef PATCH_API_H
#define PATCH_API_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct HeavyContextInterface;

typedef enum {
    PATCH_API_IN_PARAM_FLOAT = 0,
    PATCH_API_IN_PARAM_EVENT = 1,
} patch_api_in_param_type_t;

typedef struct {
    const char *name;
    uint32_t hash;
    patch_api_in_param_type_t type;
    float min_value;
    float max_value;
    float default_value;
} patch_api_in_param_t;

/**
 * Initialize Patch API and register the single send hook.
 * Must be called once after hv_patch_new(). This is the only place hv_setSendHook() is called.
 */
void patch_api_init(struct HeavyContextInterface *ctx);

/** Number of discovered patch IN params/events (valid after patch_api_init). */
uint16_t patch_api_in_param_count(void);

/** Get IN param metadata by index [0..count). Returns false on invalid args/index. */
bool patch_api_get_in_param(uint16_t index, patch_api_in_param_t *out);

/** Find IN param metadata by canonical name. Returns false when absent. */
bool patch_api_find_in_param(const char *name, patch_api_in_param_t *out);

/** MIDI: push to __hv_notein. Args: pitch, velocity, channel (0..15). */
bool patch_api_push_notein(uint8_t note, uint8_t vel, uint8_t ch);
/** MIDI: push to __hv_ctlin. Args in message: (value, cc, channel0). */
bool patch_api_push_ctlin(uint8_t ctrl, uint8_t val, uint8_t ch);
/** MIDI: push to __hv_bendin. Args: bend (0..16383), channel. */
bool patch_api_push_bendin(int16_t bend, uint8_t ch);
/** MIDI: push to __hv_pgmin. Args: program, channel. */
bool patch_api_push_pgmin(uint8_t prog, uint8_t ch);
/** MIDI: push to __hv_touchin. Args: pressure, channel. */
bool patch_api_push_touchin(uint8_t pressure, uint8_t ch);
/** MIDI: push to __hv_polytouchin. Args in message: (pressure, note, channel0). */
bool patch_api_push_polytouchin(uint8_t note, uint8_t pressure, uint8_t ch);

/** Push potentiometer value to knob1..knob4 receiver by index (0..3). Use when POTS_BACKEND=ADC. */
bool patch_api_push_knob(uint8_t index, float value);

/** Push capacitive touch state to touch1..touch12 receiver by index (0-based). */
bool patch_api_push_touch(uint8_t index, bool touched);

/** Push capacitive touch level to touch1_level..touch12_level receiver by index (0-based). */
bool patch_api_push_touch_level(uint8_t index, float level);

/**
 * Transport push counters for backpressure/drop observability.
 *
 * Counters include total push outcomes and source-based breakdown:
 * - ctrl_queue sources: usb_midi, adc_pots, mpr121
 * - led_mailbox source: patch_send_hook
 */
typedef struct {
    uint32_t ctrl_queue_push_ok;
    uint32_t ctrl_queue_push_drop;
    uint32_t led_mailbox_publish;

    uint32_t ctrl_queue_push_ok_usb_midi;
    uint32_t ctrl_queue_push_drop_usb_midi;
    uint32_t ctrl_queue_push_ok_adc_pots;
    uint32_t ctrl_queue_push_drop_adc_pots;
    uint32_t ctrl_queue_push_ok_mpr121;
    uint32_t ctrl_queue_push_drop_mpr121;

    uint32_t led_mailbox_publish_patch_send_hook;
} patch_api_transport_stats_t;

/** Get current transport push counters snapshot. */
void patch_api_get_transport_stats(patch_api_transport_stats_t *out);

/** Reset all transport push counters to zero. */
void patch_api_reset_transport_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_API_H */
