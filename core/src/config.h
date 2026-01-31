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

// USB Audio (UAC2 speaker / host -> device)
// Ring buffer between core0 (USB) and core1 (audio DSP).
#ifndef USB_AUDIO_RING_FRAMES
#define USB_AUDIO_RING_FRAMES 2048
#endif

#ifndef USB_AUDIO_TARGET_FILL_FRAMES
#ifdef ENABLE_OLED
// With OLED enabled, core0 can be intermittently busy (I2C transfers, rendering).
// A deeper inter-core target buffer helps avoid audible underruns.
#define USB_AUDIO_TARGET_FILL_FRAMES 1024
#else
#define USB_AUDIO_TARGET_FILL_FRAMES 512
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

// I2S output buffering and DMA tuning
//
// pico-extras (audio_i2s.c) will output a fixed-length silence buffer when it can't pull
// the next consumer buffer in time. Increasing consumer buffering reduces the chance of
// audible dropouts under bus/DMA contention (e.g., concurrent I2C OLED updates).
#ifndef AUDIO_I2S_CONSUMER_BUFFER_COUNT
#ifdef ENABLE_USB_AUDIO
#define AUDIO_I2S_CONSUMER_BUFFER_COUNT 4
#else
#define AUDIO_I2S_CONSUMER_BUFFER_COUNT 2
#endif
#endif

#ifndef AUDIO_I2S_CONSUMER_SAMPLES_PER_BUFFER
#define AUDIO_I2S_CONSUMER_SAMPLES_PER_BUFFER 256
#endif

// Prefer the I2S DMA channel in the DMA scheduler. This may help when other DMA channels
// (e.g., I2C DMA) run concurrently.
#ifndef AUDIO_I2S_DMA_HIGH_PRIORITY
#ifdef ENABLE_USB_AUDIO
#define AUDIO_I2S_DMA_HIGH_PRIORITY 1
#else
#define AUDIO_I2S_DMA_HIGH_PRIORITY 0
#endif
#endif

// Potentiometers (knob1..knob4 @hv_param). Single axis: config only, no CMake flag.
// POTS_BACKEND: NONE = no hardware, ADC = on-chip ADC, EXPANDER = future.
#define POTS_BACKEND_NONE    0
#define POTS_BACKEND_ADC     1
#define POTS_BACKEND_EXPANDER 2

#ifndef POTS_BACKEND
#define POTS_BACKEND POTS_BACKEND_NONE
#endif

#ifndef POTS_COUNT
#define POTS_COUNT 4
#endif

/** Maximum number of knob channels (array sizes, loop bounds). Kept at 4; no expansion in this iteration. */
#ifndef POTS_MAX
#define POTS_MAX 4
#endif

/** First ADC channel for knob1; channels are consecutive (first, first+1, ...); count = POTS_COUNT.
 *  Physical pin = ADC_BASE_PIN + channel (from SDK; varies by chip, e.g. RP2040/RP2350A 26–29, RP2350B 40–47). */
#ifndef POTS_ADC_FIRST_CHANNEL
#define POTS_ADC_FIRST_CHANNEL 0
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
// OLED refresh rate:
// - USB Audio can generate frequent USB events (control probes + isoch transfers).
// - The SSD1306 full-framebuffer I2C transfer can block core0 for multiple milliseconds.
// Lowering the refresh rate reduces worst-case USB service jitter.
#ifdef ENABLE_USB_AUDIO
#define OLED_REFRESH_FPS 20
#else
#define OLED_REFRESH_FPS 60
#endif
#endif

// While USB audio is actively streaming, OLED refresh can still introduce periodic load spikes
// (notably every 1000/OLED_REFRESH_FPS ms). Use a much lower rate while streaming to keep audio stable.
#ifndef OLED_REFRESH_FPS_STREAMING
#ifdef ENABLE_USB_AUDIO
#define OLED_REFRESH_FPS_STREAMING 2
#else
#define OLED_REFRESH_FPS_STREAMING OLED_REFRESH_FPS
#endif
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

// While USB audio is streaming, OLED updates must not block core0 for long. The firmware
// sends OLED data in small I2C chunks and services USB between them. Smaller values reduce
// worst-case USB jitter at the cost of more I2C transactions.
#ifndef OLED_I2C_STREAM_CHUNK_BYTES
#define OLED_I2C_STREAM_CHUNK_BYTES 32
#endif

// How many SSD1306 pages (8px rows) are updated during USB-audio streaming.
// Each page is OLED_WIDTH bytes on the I2C bus (e.g., 128 bytes).
#ifndef OLED_STREAMING_PAGES
#define OLED_STREAMING_PAGES 4
#endif

#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif

#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif

#endif // CONFIG_H
