/**
 * WS2812 LED Driver Implementation (C)
 *
 * C implementation with C-compatible API.
 * Uses PIO state machine for precise timing required by WS2812 protocol.
 *
 * Heavy integration is also done in C via HvHeavy.h (hv_setSendHook).
 */

#ifdef ENABLE_WS2812

#include "ws2812.h"

#include <string.h>
#include <stdlib.h>

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/time.h"   // sleep_us, sleep_ms

#include "ws2812.pio.h"
#include "HvHeavy.h"     // HvSendHook_t, hv_setSendHook, hv_stringToHash, message API

// Static state
static PIO pio_instance = WS2812_PIO_INST;
static uint pio_sm = WS2812_SM;
static uint pio_offset = 0;
static uint num_leds = 0;
static bool initialized = false;

// Buffer for LED colors (RGB format: 0xRRGGBB)
static uint32_t *led_buffer = NULL;

static inline uint32_t rgb_to_grb(uint32_t rgb) {
  const uint8_t r = (rgb >> 16) & 0xFF;
  const uint8_t g = (rgb >> 8) & 0xFF;
  const uint8_t b = rgb & 0xFF;
  // WS2812 expects GRB: 0xGGRRBB00 for PIO (left-aligned)
  return ((uint32_t) g << 24) | ((uint32_t) r << 16) | ((uint32_t) b << 8);
}

bool ws2812_init(uint pin, uint num_leds_param) {
  if (initialized) return false;
  if (num_leds_param == 0) return false;

  num_leds = num_leds_param;

  led_buffer = (uint32_t *) calloc((size_t) num_leds, sizeof(uint32_t));
  if (!led_buffer) return false;

  // Load PIO program
  pio_offset = pio_add_program(pio_instance, &ws2812_program);
  if ((int) pio_offset < 0) {
    free(led_buffer);
    led_buffer = NULL;
    return false;
  }

  // Configure GPIO pin
  pio_gpio_init(pio_instance, pin);
  pio_sm_set_consecutive_pindirs(pio_instance, pio_sm, pin, 1, true);

  // Configure state machine
  pio_sm_config c = ws2812_program_get_default_config(pio_offset);
  sm_config_set_sideset_pins(&c, pin);
  sm_config_set_out_shift(&c, false, true, 24); // 24 bits RGB
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  // Clock divider
  const int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
  const float div = (float) clock_get_hz(clk_sys) / ((float) WS2812_FREQ * (float) cycles_per_bit);
  sm_config_set_clkdiv(&c, div);

  // Init + enable
  pio_sm_init(pio_instance, pio_sm, pio_offset, &c);
  pio_sm_set_enabled(pio_instance, pio_sm, true);

  initialized = true;
  return true;
}

bool ws2812_set_color(uint index, uint32_t rgb) {
  if (!initialized || !led_buffer) return false;
  if (index >= num_leds) return false;

  led_buffer[index] = rgb;
  // Auto-update for a single LED (common case)
  if (num_leds == 1) ws2812_update();
  return true;
}

void ws2812_set_all(uint32_t rgb) {
  if (!initialized || !led_buffer) return;
  for (uint i = 0; i < num_leds; i++) led_buffer[i] = rgb;
  ws2812_update();
}

void ws2812_update(void) {
  if (!initialized || !led_buffer) return;

  for (uint i = 0; i < num_leds; i++) {
    const uint32_t grb = rgb_to_grb(led_buffer[i]);
    pio_sm_put_blocking(pio_instance, pio_sm, grb);
  }

  // Reset pulse (>50us low) after sending data
  sleep_us(100);
}

uint ws2812_get_num_leds(void) {
  return num_leds;
}

// --- Heavy send hook integration (Pd -> Firmware) ---

static void hv_send_hook(HeavyContextInterface *context,
                         const char *sendName,
                         hv_uint32_t sendHash,
                         const HvMessage *msg) {
  (void) context;
  (void) sendName;

  if (!msg) return;

  static hv_uint32_t hash_set_led_color = 0;
  static hv_uint32_t hash_set_led_index = 0;
  static bool hashes_initialized = false;

  if (!hashes_initialized) {
    hash_set_led_color = hv_stringToHash("set_led_color");
    hash_set_led_index = hv_stringToHash("set_led_index");
    hashes_initialized = true;
  }

  // Helper: accept either 0..1 floats or 0..255 floats
  const float scale = 255.0f;

  if (sendHash == hash_set_led_color) {
    // (R, G, B)
    if (hv_msg_getNumElements(msg) >= 3) {
      float r = hv_msg_getFloat(msg, 0);
      float g = hv_msg_getFloat(msg, 1);
      float b = hv_msg_getFloat(msg, 2);

      // Heuristic: if values look like 0..1, scale to 0..255
      if (r <= 1.0f && g <= 1.0f && b <= 1.0f) {
        r *= scale; g *= scale; b *= scale;
      }

      if (r < 0) r = 0; if (r > 255) r = 255;
      if (g < 0) g = 0; if (g > 255) g = 255;
      if (b < 0) b = 0; if (b > 255) b = 255;

      const uint32_t rgb = ((uint32_t) (uint8_t) r << 16) |
                           ((uint32_t) (uint8_t) g << 8)  |
                           ((uint32_t) (uint8_t) b);
      ws2812_set_all(rgb);
    }
    return;
  }

  if (sendHash == hash_set_led_index) {
    // (index, R, G, B)
    if (hv_msg_getNumElements(msg) >= 4) {
      const int idx = (int) hv_msg_getFloat(msg, 0);
      float r = hv_msg_getFloat(msg, 1);
      float g = hv_msg_getFloat(msg, 2);
      float b = hv_msg_getFloat(msg, 3);

      if (r <= 1.0f && g <= 1.0f && b <= 1.0f) {
        r *= scale; g *= scale; b *= scale;
      }

      if (r < 0) r = 0; if (r > 255) r = 255;
      if (g < 0) g = 0; if (g > 255) g = 255;
      if (b < 0) b = 0; if (b > 255) b = 255;

      if (idx >= 0 && (uint) idx < ws2812_get_num_leds()) {
        const uint32_t rgb = ((uint32_t) (uint8_t) r << 16) |
                             ((uint32_t) (uint8_t) g << 8)  |
                             ((uint32_t) (uint8_t) b);
        ws2812_set_color((uint) idx, rgb);
        if (ws2812_get_num_leds() > 1) ws2812_update();
      }
    }
    return;
  }
}

static void ws2812_register_send_hook(HeavyContextInterface *context) {
  if (!context) return;
  hv_setSendHook(context, hv_send_hook);
}

bool ws2812_init_with_hook(uint pin, uint num_leds_param, HeavyContextInterface *context) {
  if (!ws2812_init(pin, num_leds_param)) return false;

  ws2812_register_send_hook(context);

  // Blink once (white) to indicate successful initialization
  ws2812_set_all(0xFF0000);
  ws2812_update();
  sleep_ms(100);
  ws2812_set_all(0x000000);
  ws2812_update();

  return true;
}

#endif // ENABLE_WS2812

