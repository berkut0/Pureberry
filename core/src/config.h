/**
 * Firmware configuration (public compile-time options).
 *
 * Prefer local overrides in `config_local.h` (see `config_local.h.example`).
 */

#ifndef CONFIG_H
#define CONFIG_H

// Optional local overrides (not tracked in git)
// If supported by the compiler, include config_local.h when present.
#if defined(__has_include)
#  if __has_include("config_local.h")
#    include "config_local.h"
#  endif
#endif

// Feature flags are typically set by CMake:
// - ENABLE_WS2812
// - ENABLE_USB_MIDI

// USB MIDI
#ifdef ENABLE_USB_MIDI
// MIDI device name shown by the host OS.
#ifndef USB_MIDI_DEVICE_NAME
#define USB_MIDI_DEVICE_NAME "Pure Data MIDI"
#endif

#endif

// I2S (pico-extras / PICO_AUDIO_I2S_*)
// Clock pins are consecutive; order is controlled by PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED.
// Defaults free GPIO 26-29 for ADC potentiometers.

#ifndef PICO_AUDIO_I2S_DATA_PIN
#define PICO_AUDIO_I2S_DATA_PIN 5
#endif

#ifndef PICO_AUDIO_I2S_CLOCK_PIN_BASE
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 6
#endif

// 0: base = LRCLK (LCK), base+1 = BCLK (BCK)
// 1: base = BCLK (BCK),  base+1 = LRCLK (LCK)
#ifndef PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#endif

// Potentiometers (knob1..knob4 @hv_param). Single axis: config only, no CMake flag.
// POTS_BACKEND: NONE = no hardware, ADC = on-chip ADC (GPIO 26-29), EXPANDER = future.
#define POTS_BACKEND_NONE    0
#define POTS_BACKEND_ADC     1
#define POTS_BACKEND_EXPANDER 2

#ifndef POTS_BACKEND
#define POTS_BACKEND POTS_BACKEND_ADC
#endif

#ifndef POTS_COUNT
#define POTS_COUNT 4
#endif

#ifndef POTS_ADC_GPIO_0
#define POTS_ADC_GPIO_0 26
#endif
#ifndef POTS_ADC_GPIO_1
#define POTS_ADC_GPIO_1 27
#endif
#ifndef POTS_ADC_GPIO_2
#define POTS_ADC_GPIO_2 28
#endif
#ifndef POTS_ADC_GPIO_3
#define POTS_ADC_GPIO_3 29
#endif

// Poll/smoothing: avoid zipper noise and ctrl_queue overflow (drop newest).
#ifndef POTS_POLL_MS
#define POTS_POLL_MS 8
#endif
#ifndef POTS_EPS
#define POTS_EPS 0.005f
#endif
#ifndef POTS_ALPHA
#define POTS_ALPHA 0.25f
#endif

// SSD1306 OLED (u8g2) over I2C
// Notes:
// - Default wiring requested: SDA=GPIO2, SCL=GPIO3, addr=0x3C.
// - Pico SDK uses two I2C instances (i2c0, i2c1). GPIO2/3 commonly map to I2C1.
// - Refresh rate: set OLED_REFRESH_FPS; frame period and waveform decimation are derived.

#ifndef OLED_REFRESH_FPS
#define OLED_REFRESH_FPS 60
#endif

#ifndef OLED_I2C_INSTANCE
#define OLED_I2C_INSTANCE 1   // 0 = i2c0, 1 = i2c1
#endif

#ifndef OLED_I2C_SDA_PIN
#define OLED_I2C_SDA_PIN 2
#endif

#ifndef OLED_I2C_SCL_PIN
#define OLED_I2C_SCL_PIN 3
#endif

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif

#ifndef OLED_I2C_BAUD
#define OLED_I2C_BAUD 1000000  // Hz (1 MHz recommended for OLED_REFRESH_FPS 60)
#endif

#ifndef OLED_I2C_TIMEOUT_US
// Timeout for a whole I2C transaction. Used to prevent a stuck bus from hanging core0.
#define OLED_I2C_TIMEOUT_US 5000
#endif

#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif

#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif

#endif // CONFIG_H
