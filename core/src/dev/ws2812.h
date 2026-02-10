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
/**
 * Initialize WS2812 and blink once to indicate ready state.
 *
 * This is a convenience helper around ws2812_init() that blinks the LED(s)
 * once on success. It does not register any Heavy send hook (patch_api.c owns
 * the single hv_setSendHook() entry point).
 *
 * @param pin GPIO pin number for WS2812 data line
 * @param num_leds Number of LEDs in the strip/chain
 * @return true if initialization successful, false otherwise
 */
bool ws2812_init_with_status_blink(uint pin, uint num_leds);
#endif // ENABLE_WS2812

#ifdef __cplusplus
}
#endif

#endif // WS2812_H
