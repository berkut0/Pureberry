/**
 * WS2812 LED Driver
 * 
 * C-compatible API for controlling WS2812 addressable LEDs via PIO.
 * Supports optional compilation via ENABLE_WS2812 define.
 */

#ifndef WS2812_H
#define WS2812_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pico/types.h"

// Configuration defaults (can be overridden before including this header)
#ifndef WS2812_PIN
#define WS2812_PIN 16  // Default GPIO pin
#endif

#ifndef WS2812_NUM_LEDS
#define WS2812_NUM_LEDS 1  // Default number of LEDs
#endif

#ifndef WS2812_PIO_INST
#define WS2812_PIO_INST pio0  // Default PIO instance
#endif

#ifndef WS2812_SM
#define WS2812_SM 1  // Default state machine (SM 0 is used by I2S)
#endif

#ifndef WS2812_FREQ
#define WS2812_FREQ 800000  // Default frequency: 800 kHz (standard for WS2812)
#endif

/**
 * Initialize WS2812 driver
 * 
 * @param pin GPIO pin number for WS2812 data line
 * @param num_leds Number of LEDs in the strip/chain
 * @return true if initialization successful, false otherwise
 */
bool ws2812_init(uint pin, uint num_leds);

/**
 * Set color of a specific LED
 * 
 * @param index LED index (0-based)
 * @param rgb Color in 0xRRGGBB format (24-bit RGB)
 * @return true if successful, false if index out of range
 */
bool ws2812_set_color(uint index, uint32_t rgb);

/**
 * Set all LEDs to the same color
 * 
 * @param rgb Color in 0xRRGGBB format (24-bit RGB)
 */
void ws2812_set_all(uint32_t rgb);

/**
 * Update LEDs (send data to hardware)
 * 
 * This function sends buffered color data to the WS2812 LEDs.
 * For single LED, this is called automatically. For multiple LEDs,
 * call this after setting all colors.
 */
void ws2812_update(void);

/**
 * Get number of LEDs configured
 * 
 * @return Number of LEDs
 */
uint ws2812_get_num_leds(void);

#ifdef ENABLE_WS2812
// Forward declarations for Heavy types
// Full definition is not needed here; `ws2812.c` uses HvHeavy.h C API (hv_setSendHook).
#ifndef HeavyContextInterface_DEFINED
typedef struct HeavyContextInterface HeavyContextInterface;
#define HeavyContextInterface_DEFINED
#endif
#ifndef HvMessage_DEFINED
typedef struct HvMessage HvMessage;
#define HvMessage_DEFINED
#endif
#ifndef hv_uint32_t_DEFINED
typedef uint32_t hv_uint32_t;
#define hv_uint32_t_DEFINED
#endif

/**
 * Initialize WS2812 and register send hook for Pure Data control
 * 
 * This function initializes WS2812 driver and registers a send hook handler
 * that processes messages from Pure Data patches to control WS2812 LEDs.
 * 
 * On successful initialization, LED will blink once (white) to indicate ready state.
 * 
 * Supported send channels:
 * - set_led_color: Sets LED color (3 floats: R, G, B in range 0-1 or 0-255)
 * - set_led_index: Sets specific LED color (4 floats: index, R, G, B)
 * 
 * @param pin GPIO pin number for WS2812 data line
 * @param num_leds Number of LEDs in the strip/chain
 * @param context Heavy context interface (must be initialized)
 * @return true if initialization successful, false otherwise
 */
bool ws2812_init_with_hook(uint pin, uint num_leds, HeavyContextInterface *context);
#endif // ENABLE_WS2812

#ifdef __cplusplus
}
#endif

#endif // WS2812_H
