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
// - ENABLE_USB_AUDIO

// System clock profile (RP2350).
// Default profile keeps pico-sdk defaults (150 MHz on RP2350).
// OC 240 profile sets a known-valid PLL tuple from pico-sdk vcocalc.py.
// Notes:
// - Overclocking is experimental; validate on target hardware under load.
// - Boot stage2 (flash/QSPI bring-up) is a separate build target and does not
//   include config_local.h. Tune PICO_FLASH_SPI_CLKDIV via CMake (-D) if needed.
#define FW_SYS_CLOCK_PROFILE_DEFAULT   0
#define FW_SYS_CLOCK_PROFILE_OC_240MHZ 1

#ifndef FW_SYS_CLOCK_PROFILE
#define FW_SYS_CLOCK_PROFILE FW_SYS_CLOCK_PROFILE_DEFAULT
#endif

#if FW_SYS_CLOCK_PROFILE == FW_SYS_CLOCK_PROFILE_DEFAULT
/* Keep SDK defaults unless explicitly overridden below/in config_local.h */
#elif FW_SYS_CLOCK_PROFILE == FW_SYS_CLOCK_PROFILE_OC_240MHZ
/* 240 MHz = 12 MHz / 1 * 120 / 6 / 1 (VCO 1.44 GHz) */
#ifndef SYS_CLK_HZ
#define SYS_CLK_HZ 240000000
#endif
#ifndef PLL_SYS_REFDIV
#define PLL_SYS_REFDIV 1
#endif
#ifndef PLL_SYS_VCO_FREQ_HZ
#define PLL_SYS_VCO_FREQ_HZ 1440000000
#endif
#ifndef PLL_SYS_POSTDIV1
#define PLL_SYS_POSTDIV1 6
#endif
#ifndef PLL_SYS_POSTDIV2
#define PLL_SYS_POSTDIV2 1
#endif
/* Raise VREG floor for OC profile (SDK does not auto-adjust on RP2350). */
#ifndef SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST
#define SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST 1
#endif
#ifndef SYS_CLK_VREG_VOLTAGE_MIN
#define SYS_CLK_VREG_VOLTAGE_MIN VREG_VOLTAGE_1_15
#endif
#else
#error "Unsupported FW_SYS_CLOCK_PROFILE value"
#endif

// USB MIDI
#ifdef ENABLE_USB_MIDI
// MIDI device name shown by the host OS.
#ifndef USB_MIDI_DEVICE_NAME
#define USB_MIDI_DEVICE_NAME "Pure Data MIDI"
#endif

#endif

// USB Audio (UAC2 speaker / host -> device)
#ifndef USB_AUDIO_RING_FRAMES
#define USB_AUDIO_RING_FRAMES 2048
#endif

#ifndef USB_AUDIO_TARGET_FILL_FRAMES
#ifdef ENABLE_OLED
#define USB_AUDIO_TARGET_FILL_FRAMES 1024
#else
#define USB_AUDIO_TARGET_FILL_FRAMES 512
#endif
#endif

// USB bring-up / main-loop timing
#ifndef USB_INIT_ITERATIONS
#define USB_INIT_ITERATIONS 100
#endif

#ifndef USB_INIT_DELAY_MS
#define USB_INIT_DELAY_MS 1
#endif

#ifndef MAIN_LOOP_SLEEP_US
#define MAIN_LOOP_SLEEP_US 100
#endif

// UI input (buttons/gestures -> ui_action_t)
#ifndef UI_INPUT_ENABLED
#  ifdef ENABLE_OLED
#    define UI_INPUT_ENABLED 1
#  else
#    define UI_INPUT_ENABLED 0
#  endif
#endif

// MultiButton's default timing model assumes a 5 ms periodic tick.
#ifndef UI_INPUT_TICK_MS
#define UI_INPUT_TICK_MS 5
#endif

#ifndef UI_SYSTEM_STATS_REFRESH_MS
#define UI_SYSTEM_STATS_REFRESH_MS 1000
#endif

#ifndef UI_INPUT_EVENT_QUEUE_SIZE
#define UI_INPUT_EVENT_QUEUE_SIZE 16
#endif

#ifndef UI_BTN_ACTIVE_LOW
#define UI_BTN_ACTIVE_LOW 1
#endif

#ifndef UI_BTN_LEFT_PIN
#define UI_BTN_LEFT_PIN 15
#endif

#ifndef UI_BTN_CENTER_PIN
#define UI_BTN_CENTER_PIN 14
#endif

#ifndef UI_BTN_RIGHT_PIN
#define UI_BTN_RIGHT_PIN 13
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

// I2C bus configuration (OLED, MPR121, future I2C devices)
// Defaults preserve current OLED wiring (GPIO2/3, I2C1, 1MHz).
// Bus 0 is the default for all devices unless a device selects bus 1.
#ifndef I2C_BUS0_INSTANCE
#  ifdef I2C_BUS_INSTANCE
#    define I2C_BUS0_INSTANCE I2C_BUS_INSTANCE
#  elif defined(OLED_I2C_INSTANCE)
#    define I2C_BUS0_INSTANCE OLED_I2C_INSTANCE
#  else
#    define I2C_BUS0_INSTANCE 1   // 0 = i2c0, 1 = i2c1
#  endif
#endif

#ifndef I2C_BUS0_SDA_PIN
#  ifdef I2C_BUS_SDA_PIN
#    define I2C_BUS0_SDA_PIN I2C_BUS_SDA_PIN
#  elif defined(OLED_I2C_SDA_PIN)
#    define I2C_BUS0_SDA_PIN OLED_I2C_SDA_PIN
#  else
#    define I2C_BUS0_SDA_PIN 2
#  endif
#endif

#ifndef I2C_BUS0_SCL_PIN
#  ifdef I2C_BUS_SCL_PIN
#    define I2C_BUS0_SCL_PIN I2C_BUS_SCL_PIN
#  elif defined(OLED_I2C_SCL_PIN)
#    define I2C_BUS0_SCL_PIN OLED_I2C_SCL_PIN
#  else
#    define I2C_BUS0_SCL_PIN 3
#  endif
#endif

#ifndef I2C_BUS0_BAUD
#  ifdef I2C_BUS_BAUD
#    define I2C_BUS0_BAUD I2C_BUS_BAUD
#  elif defined(OLED_I2C_BAUD)
#    define I2C_BUS0_BAUD OLED_I2C_BAUD
#  else
#    define I2C_BUS0_BAUD 1000000  // Hz (1 MHz recommended for OLED_REFRESH_FPS 60)
#  endif
#endif

#ifndef I2C_BUS0_TIMEOUT_US
#  ifdef I2C_BUS_TIMEOUT_US
#    define I2C_BUS0_TIMEOUT_US I2C_BUS_TIMEOUT_US
#  elif defined(OLED_I2C_TIMEOUT_US)
#    define I2C_BUS0_TIMEOUT_US OLED_I2C_TIMEOUT_US
#  else
#    define I2C_BUS0_TIMEOUT_US 5000
#  endif
#endif

// When blocking I2C transactions share a bus with DMA OLED refresh,
// waiting only I2C_BUS*_TIMEOUT_US may be too short (full-frame OLED DMA can
// keep the bus busy for >5 ms). This timeout is used for "wait until DMA idle"
// before a blocking transfer starts.
#ifndef I2C_BUS_DMA_IDLE_TIMEOUT_US
#define I2C_BUS_DMA_IDLE_TIMEOUT_US 30000
#endif

#ifndef I2C_BUS1_INSTANCE
#define I2C_BUS1_INSTANCE I2C_BUS0_INSTANCE
#endif

#ifndef I2C_BUS1_SDA_PIN
#define I2C_BUS1_SDA_PIN I2C_BUS0_SDA_PIN
#endif

#ifndef I2C_BUS1_SCL_PIN
#define I2C_BUS1_SCL_PIN I2C_BUS0_SCL_PIN
#endif

#ifndef I2C_BUS1_BAUD
#define I2C_BUS1_BAUD I2C_BUS0_BAUD
#endif

#ifndef I2C_BUS1_TIMEOUT_US
#define I2C_BUS1_TIMEOUT_US I2C_BUS0_TIMEOUT_US
#endif

// Legacy aliases (bus 0).
#ifndef I2C_BUS_INSTANCE
#define I2C_BUS_INSTANCE I2C_BUS0_INSTANCE
#endif
#ifndef I2C_BUS_SDA_PIN
#define I2C_BUS_SDA_PIN I2C_BUS0_SDA_PIN
#endif
#ifndef I2C_BUS_SCL_PIN
#define I2C_BUS_SCL_PIN I2C_BUS0_SCL_PIN
#endif
#ifndef I2C_BUS_BAUD
#define I2C_BUS_BAUD I2C_BUS0_BAUD
#endif
#ifndef I2C_BUS_TIMEOUT_US
#define I2C_BUS_TIMEOUT_US I2C_BUS0_TIMEOUT_US
#endif

// Helper macro to select i2c0/i2c1 from an instance number.
#define I2C_GET_INSTANCE(instance_num) ((instance_num) == 0 ? i2c0 : i2c1)

// SPI bus configuration (future peripherals; verify pin mux in datasheet/SDK).
#ifndef SPI_BUS0_INSTANCE
#define SPI_BUS0_INSTANCE 0  // 0 = spi0, 1 = spi1
#endif
#ifndef SPI_BUS0_SCK_PIN
#define SPI_BUS0_SCK_PIN 18
#endif
#ifndef SPI_BUS0_TX_PIN
#define SPI_BUS0_TX_PIN 19
#endif
#ifndef SPI_BUS0_RX_PIN
#define SPI_BUS0_RX_PIN 16
#endif
#ifndef SPI_BUS0_BAUD
#define SPI_BUS0_BAUD 1000000
#endif

#ifndef SPI_BUS1_INSTANCE
#define SPI_BUS1_INSTANCE SPI_BUS0_INSTANCE
#endif
#ifndef SPI_BUS1_SCK_PIN
#define SPI_BUS1_SCK_PIN SPI_BUS0_SCK_PIN
#endif
#ifndef SPI_BUS1_TX_PIN
#define SPI_BUS1_TX_PIN SPI_BUS0_TX_PIN
#endif
#ifndef SPI_BUS1_RX_PIN
#define SPI_BUS1_RX_PIN SPI_BUS0_RX_PIN
#endif
#ifndef SPI_BUS1_BAUD
#define SPI_BUS1_BAUD SPI_BUS0_BAUD
#endif

#ifndef SPI_GET_INSTANCE
#define SPI_GET_INSTANCE(instance_num) ((instance_num) == 0 ? spi0 : spi1)
#endif

// SSD1306 OLED (u8g2) over I2C
// Notes:
// - Default wiring requested: SDA=GPIO2, SCL=GPIO3, addr=0x3C.
// - Pico SDK uses two I2C instances (i2c0, i2c1). GPIO2/3 commonly map to I2C1.
// - Refresh rate: set OLED_REFRESH_FPS; frame period and waveform decimation are derived.

#ifndef OLED_REFRESH_FPS
#define OLED_REFRESH_FPS 60
#endif

#ifndef OLED_I2C_BUS_ID
#define OLED_I2C_BUS_ID 0
#endif

#ifndef MPR121_I2C_BUS_ID
#define MPR121_I2C_BUS_ID 0
#endif

#if OLED_I2C_BUS_ID == 0
#  ifndef OLED_I2C_INSTANCE
#    define OLED_I2C_INSTANCE I2C_BUS0_INSTANCE
#  endif
#  ifndef OLED_I2C_SDA_PIN
#    define OLED_I2C_SDA_PIN I2C_BUS0_SDA_PIN
#  endif
#  ifndef OLED_I2C_SCL_PIN
#    define OLED_I2C_SCL_PIN I2C_BUS0_SCL_PIN
#  endif
#  ifndef OLED_I2C_BAUD
#    define OLED_I2C_BAUD I2C_BUS0_BAUD
#  endif
#  ifndef OLED_I2C_TIMEOUT_US
// Timeout for a whole I2C transaction. Used to prevent a stuck bus from hanging core0.
#    define OLED_I2C_TIMEOUT_US I2C_BUS0_TIMEOUT_US
#  endif
#elif OLED_I2C_BUS_ID == 1
#  ifndef OLED_I2C_INSTANCE
#    define OLED_I2C_INSTANCE I2C_BUS1_INSTANCE
#  endif
#  ifndef OLED_I2C_SDA_PIN
#    define OLED_I2C_SDA_PIN I2C_BUS1_SDA_PIN
#  endif
#  ifndef OLED_I2C_SCL_PIN
#    define OLED_I2C_SCL_PIN I2C_BUS1_SCL_PIN
#  endif
#  ifndef OLED_I2C_BAUD
#    define OLED_I2C_BAUD I2C_BUS1_BAUD
#  endif
#  ifndef OLED_I2C_TIMEOUT_US
// Timeout for a whole I2C transaction. Used to prevent a stuck bus from hanging core0.
#    define OLED_I2C_TIMEOUT_US I2C_BUS1_TIMEOUT_US
#  endif
#else
#  error "OLED_I2C_BUS_ID must be 0 or 1"
#endif

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif

#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif

#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif

// MPR121 capacitive touch (shares I2C bus with OLED; same instance/pins)
#ifndef MPR121_IRQ_PIN
#define MPR121_IRQ_PIN 12
#endif
#ifndef MPR121_I2C_ADDR
#define MPR121_I2C_ADDR 0x5A
#endif
#define MPR121_NUM_ELECTRODES 12
// Touch/release thresholds (0..255). Lower = more sensitive. Override in config_local.h if pads 2,3 etc. don't trigger.
#ifndef MPR121_TOUCH_THRESHOLD
#define MPR121_TOUCH_THRESHOLD 16
#endif
#ifndef MPR121_RELEASE_THRESHOLD
#define MPR121_RELEASE_THRESHOLD 10
#endif
#ifndef MPR121_POLL_MS
#define MPR121_POLL_MS 100
#endif

#endif // CONFIG_H
