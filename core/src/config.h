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

#ifndef PICO_AUDIO_I2S_DATA_PIN
#define PICO_AUDIO_I2S_DATA_PIN 26
#endif

#ifndef PICO_AUDIO_I2S_CLOCK_PIN_BASE
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#endif

// 0: base = LRCLK (LCK), base+1 = BCLK (BCK)
// 1: base = BCLK (BCK),  base+1 = LRCLK (LCK)
#ifndef PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#endif

#endif // CONFIG_H
