/**
 * WS2812 LED Driver Implementation (C)
 *
 * C implementation with C-compatible API.
 * Uses PIO state machine for precise timing required by WS2812 protocol.
 *
 * This driver provides init, set_color, set_all, update, get_num_leds only.
 * It does not depend on cross-core transport; application code (e.g. main.c)
 * consumes LED commands from the bus and calls this API.
 */

#ifdef ENABLE_WS2812

#include "ws2812.h"

#include <stdlib.h>

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/time.h"   // sleep_us, sleep_ms

#include "ws2812.pio.h"

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

bool ws2812_init_with_status_blink(uint pin, uint num_leds_param) {
  if (!ws2812_init(pin, num_leds_param)) return false;

  /* Blink once to indicate successful initialization. */
  ws2812_set_all(0x00AAFF);
  ws2812_update();
  sleep_ms(100);
  ws2812_set_all(0x000000);
  ws2812_update();

  return true;
}

#endif // ENABLE_WS2812
