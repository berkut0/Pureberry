/**
 * Patch API — contract between Pd patch and firmware.
 *
 * - MIDI: push to __hv_* receivers with canonical argument order (see hvcc test_midi.cpp).
 * - Send hook: hv_setSendHook() is called exactly once from patch_api_init(ctx).
 *   No driver (ws2812, future i2c/encoders/display) must ever call hv_setSendHook().
 * - Command table: patch name -> message format -> queue/handler (e.g. set_led_color, set_led_index).
 */

#ifndef PATCH_API_H
#define PATCH_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct HeavyContextInterface;

/**
 * Initialize Patch API and register the single send hook.
 * Must be called once after hv_patch_new(). This is the only place hv_setSendHook() is called.
 */
void patch_api_init(struct HeavyContextInterface *ctx);

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

#ifdef __cplusplus
}
#endif

#endif /* PATCH_API_H */
