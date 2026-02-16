/*
 * MPR121 capacitive touch driver (core0).
 * Pushes touch events via patch_api contract.
 *
 * Important: all runtime I2C access goes through i2c_bus_* so OLED DMA and
 * MPR121 do not race on the same hardware I2C peripheral.
 */

#ifdef ENABLE_MPR121

#include "dev/mpr121_touch.h"
#include "config.h"
#include "drv/i2c_bus.h"
#include "drv/i2c_reg_io.h"
#include "patch_api.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#include <stdio.h>

static bool g_ready;
static uint16_t g_last_touched;

static volatile bool g_irq_pending;

/* Polling fallback when IRQ never fires (e.g. IRQ pin not connected). */
static uint32_t g_last_poll_ms;
static bool g_touch_push_enabled[MPR121_NUM_ELECTRODES];
static bool g_touch_level_push_enabled[MPR121_NUM_ELECTRODES];
static bool g_any_touch_push_enabled;
static bool g_any_touch_level_push_enabled;
static bool g_touch_routes_enabled;

typedef enum {
    MPR121_TOUCH_STATUS_REG = 0x00u,
    MPR121_ELECTRODE_FILTERED_DATA_REG = 0x04u,
    MPR121_MAX_HALF_DELTA_RISING_REG = 0x2Bu,
    MPR121_NOISE_HALF_DELTA_RISING_REG = 0x2Cu,
    MPR121_NOISE_COUNT_LIMIT_RISING_REG = 0x2Du,
    MPR121_FILTER_DELAY_COUNT_RISING_REG = 0x2Eu,
    MPR121_MAX_HALF_DELTA_FALLING_REG = 0x2Fu,
    MPR121_NOISE_HALF_DELTA_FALLING_REG = 0x30u,
    MPR121_NOISE_COUNT_LIMIT_FALLING_REG = 0x31u,
    MPR121_FILTER_DELAY_COUNT_FALLING_REG = 0x32u,
    MPR121_NOISE_HALF_DELTA_TOUCHED_REG = 0x33u,
    MPR121_NOISE_COUNT_LIMIT_TOUCHED_REG = 0x34u,
    MPR121_FILTER_DELAY_COUNT_TOUCHED_REG = 0x35u,
    MPR121_TOUCH_THRESHOLD_REG = 0x41u,
    MPR121_RELEASE_THRESHOLD_REG = 0x42u,
    MPR121_DEBOUNCE_REG = 0x5Bu,
    MPR121_AFE_CONFIG_REG = 0x5Cu,
    MPR121_FILTER_CONFIG_REG = 0x5Du,
    MPR121_ELECTRODE_CONFIG_REG = 0x5Eu,
    MPR121_AUTOCONFIG_CONTROL_0_REG = 0x7Bu,
    MPR121_AUTOCONFIG_USL_REG = 0x7Du,
    MPR121_AUTOCONFIG_LSL_REG = 0x7Eu,
    MPR121_AUTOCONFIG_TARGET_REG = 0x7Fu,
    MPR121_SOFT_RESET_REG = 0x80u,
} mpr121_reg_t;

typedef struct {
    uint8_t reg;
    uint8_t val;
} mpr121_reg_write_t;

static inline i2c_bus_id_t mpr121_bus_id(void) {
    return (i2c_bus_id_t)MPR121_I2C_BUS_ID;
}

static inline uint8_t mpr121_addr7(void) {
    return (uint8_t)MPR121_I2C_ADDR;
}

static i2c_reg_io_result_t mpr121_write_reg(uint8_t reg, uint8_t val) {
    return i2c_reg_write_u8(mpr121_bus_id(), mpr121_addr7(), reg, val);
}

static i2c_reg_io_result_t mpr121_read_reg(uint8_t reg, uint8_t *out_val) {
    if (!out_val) return I2C_REG_IO_EINVAL;
    return i2c_reg_read_u8(mpr121_bus_id(), mpr121_addr7(), reg, out_val);
}

static i2c_reg_io_result_t mpr121_read_reg16(uint8_t reg, uint16_t *out_val) {
    if (!out_val) return I2C_REG_IO_EINVAL;
    return i2c_reg_read_u16_le(mpr121_bus_id(), mpr121_addr7(), reg, out_val);
}

static i2c_reg_io_result_t mpr121_write_thresholds(uint8_t touch, uint8_t release) {
    for (uint8_t i = 0; i < 12u; i++) {
        uint8_t touch_reg = (uint8_t)(MPR121_TOUCH_THRESHOLD_REG + i * 2u);
        uint8_t release_reg = (uint8_t)(MPR121_RELEASE_THRESHOLD_REG + i * 2u);
        i2c_reg_io_result_t res = mpr121_write_reg(touch_reg, touch);
        if (res != I2C_REG_IO_OK) return res;
        res = mpr121_write_reg(release_reg, release);
        if (res != I2C_REG_IO_OK) return res;
    }
    return I2C_REG_IO_OK;
}

static i2c_reg_io_result_t mpr121_program_defaults(void) {
    static const mpr121_reg_write_t init_writes[] = {
        { (uint8_t)MPR121_AFE_CONFIG_REG, 0x10u },
        { (uint8_t)MPR121_FILTER_CONFIG_REG, 0x20u },
        { (uint8_t)MPR121_AUTOCONFIG_USL_REG, 0xC9u },
        { (uint8_t)MPR121_AUTOCONFIG_TARGET_REG, 0xB5u },
        { (uint8_t)MPR121_AUTOCONFIG_LSL_REG, 0x83u },
        { (uint8_t)MPR121_AUTOCONFIG_CONTROL_0_REG, 0x0Bu },
        { (uint8_t)MPR121_MAX_HALF_DELTA_RISING_REG, 0x01u },
        { (uint8_t)MPR121_MAX_HALF_DELTA_FALLING_REG, 0x01u },
        { (uint8_t)MPR121_NOISE_HALF_DELTA_RISING_REG, 0x01u },
        { (uint8_t)MPR121_NOISE_HALF_DELTA_FALLING_REG, 0x01u },
        { (uint8_t)MPR121_NOISE_HALF_DELTA_TOUCHED_REG, 0x01u },
        { (uint8_t)MPR121_NOISE_COUNT_LIMIT_RISING_REG, 0x00u },
        { (uint8_t)MPR121_NOISE_COUNT_LIMIT_FALLING_REG, 0xFFu },
        { (uint8_t)MPR121_NOISE_COUNT_LIMIT_TOUCHED_REG, 0x00u },
        { (uint8_t)MPR121_FILTER_DELAY_COUNT_RISING_REG, 0x00u },
        { (uint8_t)MPR121_FILTER_DELAY_COUNT_FALLING_REG, 0x02u },
        { (uint8_t)MPR121_FILTER_DELAY_COUNT_TOUCHED_REG, 0x00u },
        { (uint8_t)MPR121_DEBOUNCE_REG, 0x00u },
    };

    i2c_reg_io_result_t res = mpr121_write_reg((uint8_t)MPR121_ELECTRODE_CONFIG_REG, 0x00u);
    if (res != I2C_REG_IO_OK) return res;
    res = mpr121_write_reg((uint8_t)MPR121_SOFT_RESET_REG, 0x63u);
    if (res != I2C_REG_IO_OK) return res;

    for (size_t i = 0; i < sizeof(init_writes) / sizeof(init_writes[0]); i++) {
        res = mpr121_write_reg(init_writes[i].reg, init_writes[i].val);
        if (res != I2C_REG_IO_OK) return res;
    }

    res = mpr121_write_thresholds((uint8_t)MPR121_TOUCH_THRESHOLD, (uint8_t)MPR121_RELEASE_THRESHOLD);
    if (res != I2C_REG_IO_OK) {
        return res;
    }

    // Run mode: baseline tracking lock mode + all 12 electrodes enabled.
    return mpr121_write_reg((uint8_t)MPR121_ELECTRODE_CONFIG_REG, 0x8Cu);
}

static void mpr121_detect_patch_touch_routes(void) {
    g_any_touch_push_enabled = false;
    g_any_touch_level_push_enabled = false;
    g_touch_routes_enabled = false;
    for (uint8_t i = 0; i < (uint8_t)MPR121_NUM_ELECTRODES; i++) {
        g_touch_push_enabled[i] = false;
        g_touch_level_push_enabled[i] = false;

        patch_api_in_param_t meta;
        char name[24];

        (void)snprintf(name, sizeof(name), "touch%u", (unsigned)(i + 1u));
        if (patch_api_find_in_param(name, &meta)) {
            g_touch_push_enabled[i] = true;
            g_any_touch_push_enabled = true;
        }

        (void)snprintf(name, sizeof(name), "touch%u_level", (unsigned)(i + 1u));
        if (patch_api_find_in_param(name, &meta)) {
            g_touch_level_push_enabled[i] = true;
            g_any_touch_level_push_enabled = true;
        }
    }
    g_touch_routes_enabled = (g_any_touch_push_enabled || g_any_touch_level_push_enabled);
}

/* Raw IRQ handler: dedicated to this pin, must acknowledge. Callback API shares one handler and can be overwritten. */
static void mpr121_raw_irq_handler(void) {
    uint32_t events = gpio_get_irq_event_mask(MPR121_IRQ_PIN);
    if (events & GPIO_IRQ_EDGE_FALL) {
        g_irq_pending = true;
        gpio_acknowledge_irq(MPR121_IRQ_PIN, GPIO_IRQ_EDGE_FALL);
    }
}

/** Probe I2C: try to read one byte from MPR121. */
static i2c_reg_io_result_t mpr121_probe(void) {
    uint8_t dummy = 0u;
    return mpr121_read_reg((uint8_t)MPR121_TOUCH_STATUS_REG, &dummy);
}

bool mpr121_touch_init(void) {
    if (g_ready) return true;

    if (!i2c_bus_init_once(mpr121_bus_id())) {
        return false;
    }

    uint32_t bus_baud_hz = i2c_bus_get_baud_hz(mpr121_bus_id());
    if (bus_baud_hz > 400000u) {
        printf(
            "MPR121: warning - bus baud %lu Hz is above MPR121 datasheet guaranteed range (400000 Hz); may still work depending on hardware\n",
            (unsigned long)bus_baud_hz
        );
    }

    i2c_reg_io_result_t probe = mpr121_probe();
    if (probe != I2C_REG_IO_OK) {
        printf("MPR121: no device at I2C addr 0x%02X (probe NACK)\n", (unsigned)mpr121_addr7());
        return false;
    }

    /* IRQ pin: MPR121 is open-drain, active-low. Use raw handler so this pin has a dedicated handler (shared callback can be overwritten). */
    gpio_init(MPR121_IRQ_PIN);
    gpio_set_dir(MPR121_IRQ_PIN, GPIO_IN);
    gpio_pull_up(MPR121_IRQ_PIN);
    g_irq_pending = false;
    gpio_add_raw_irq_handler_masked(1u << MPR121_IRQ_PIN, mpr121_raw_irq_handler);
    gpio_set_irq_enabled(MPR121_IRQ_PIN, GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);

    if (mpr121_program_defaults() != I2C_REG_IO_OK) {
        printf("MPR121: register init failed\n");
        return false;
    }

    mpr121_detect_patch_touch_routes();
    if (!g_touch_routes_enabled) {
        // Patch has no touch inputs: keep sensor initialized, but do not run runtime reads/IRQs.
        gpio_set_irq_enabled(MPR121_IRQ_PIN, GPIO_IRQ_EDGE_FALL, false);
        g_irq_pending = false;
        printf("MPR121: no touch routes in patch; runtime reads disabled\n");
    }
    g_last_touched = 0;
    g_last_poll_ms = to_ms_since_boot(get_absolute_time());
    g_ready = true;
    return true;
}

#define MPR121_FILTERED_DATA_MAX 1023.0f  /* 10-bit filtered data */

static i2c_reg_io_result_t mpr121_read_and_push(void) {
    uint16_t touched = 0u;
    i2c_reg_io_result_t res = mpr121_read_reg16((uint8_t)MPR121_TOUCH_STATUS_REG, &touched);
    if (res != I2C_REG_IO_OK) {
        return res;
    }
    touched &= 0x0fffu;

    for (unsigned i = 0; i < (unsigned)MPR121_NUM_ELECTRODES; i++) {
        if (!g_touch_push_enabled[i]) continue;
        uint16_t mask = (uint16_t)(1u << i);
        bool now = (touched & mask) != 0;
        bool prev = (g_last_touched & mask) != 0;
        if (now != prev) {
            (void)patch_api_push_touch((uint8_t)i, now);
        }
    }
    g_last_touched = touched;

    if (!g_any_touch_level_push_enabled) {
        return I2C_REG_IO_OK;
    }

    /* Push filtered data (0..1) only for mapped touchN_level inputs. */
    for (unsigned i = 0; i < (unsigned)MPR121_NUM_ELECTRODES; i++) {
        if (!g_touch_level_push_enabled[i]) continue;
        uint16_t raw = 0u;
        uint8_t reg = (uint8_t)(MPR121_ELECTRODE_FILTERED_DATA_REG + ((uint8_t)i * 2u));
        res = mpr121_read_reg16(reg, &raw);
        if (res != I2C_REG_IO_OK) {
            return res;
        }
        raw &= 0x03ffu;
        float level = (float)raw / MPR121_FILTERED_DATA_MAX;
        if (level > 1.0f) level = 1.0f;
        (void)patch_api_push_touch_level((uint8_t)i, level);
    }

    return I2C_REG_IO_OK;
}

void mpr121_touch_task(void) {
    if (!g_ready) return;
    if (!g_touch_routes_enabled) return;

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    bool do_read = g_irq_pending;

    if (!do_read && (now_ms - g_last_poll_ms >= MPR121_POLL_MS)) {
        /* Polling fallback: IRQ may be not connected; read periodically so OLED and Pd get updates. */
        g_last_poll_ms = now_ms;
        do_read = true;
    }

    if (do_read) {
        g_irq_pending = false;
        i2c_reg_io_result_t read_res = mpr121_read_and_push();
        if (read_res == I2C_REG_IO_OK) {
            return;
        }

        if (read_res == I2C_REG_IO_EBUSY) {
            // Soft contention: retry later without forcing recover path.
            g_irq_pending = true;
            return;
        }

        if (read_res == I2C_REG_IO_EIO || read_res == I2C_REG_IO_ETIMEOUT) {
            // Transport failure path: recover bus then re-apply device defaults.
            (void)i2c_bus_recover(mpr121_bus_id());
            if (mpr121_program_defaults() == I2C_REG_IO_OK) {
                g_last_touched = 0u;
            }
        }
    }
}

#endif /* ENABLE_MPR121 */
