/*
 * MPR121 capacitive touch driver (core0).
 * Uses pico-mpr121; pushes touch1..touch12 (@hv_param) via ctrl_queue.
 */

#ifdef ENABLE_MPR121

#include "dev/mpr121_touch.h"
#include "config.h"
#include "drv/i2c_bus.h"
#include "multicore_audio.h"
#include "HvHeavy.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "mpr121.h"

#include <stdio.h>

#define MPR121_TOUCH_NAMES MPR121_NUM_ELECTRODES
#define MPR121_NAME_BUFFER_SIZE 16

static mpr121_sensor_t g_sensor;
static bool g_ready;
static uint16_t g_last_touched;
static uint32_t g_hash_touch[MPR121_TOUCH_NAMES];
static uint32_t g_hash_touch_level[MPR121_TOUCH_NAMES];
static bool g_hashes_done;

static volatile bool g_irq_pending;

/* Polling fallback when IRQ never fires (e.g. IRQ pin not connected). */
static uint32_t g_last_poll_ms;

/* Raw IRQ handler: dedicated to this pin, must acknowledge. Callback API shares one handler and can be overwritten. */
static void mpr121_raw_irq_handler(void) {
    uint32_t events = gpio_get_irq_event_mask(MPR121_IRQ_PIN);
    if (events & GPIO_IRQ_EDGE_FALL) {
        g_irq_pending = true;
        gpio_acknowledge_irq(MPR121_IRQ_PIN, GPIO_IRQ_EDGE_FALL);
    }
}

static i2c_inst_t *mpr121_get_i2c(void) {
    return i2c_bus_get_inst((i2c_bus_id_t)MPR121_I2C_BUS_ID);
}

static void ensure_touch_hashes(void) {
    if (g_hashes_done) return;
    for (int i = 0; i < MPR121_TOUCH_NAMES; i++) {
        char name[MPR121_NAME_BUFFER_SIZE];
        snprintf(name, sizeof(name), "touch%u", (unsigned)(i + 1));
        g_hash_touch[i] = (uint32_t) hv_stringToHash(name);
        snprintf(name, sizeof(name), "touch%u_level", (unsigned)(i + 1));
        g_hash_touch_level[i] = (uint32_t) hv_stringToHash(name);
    }
    g_hashes_done = true;
}

/** Probe I2C: try to read one byte from MPR121. Returns true if device ACKs. */
static bool mpr121_probe(i2c_bus_id_t bus_id, uint8_t addr) {
    uint8_t reg = 0x00;  /* touch status register, read-only */
    int nw = i2c_bus_write_timeout(bus_id, addr, &reg, 1, true);
    if (nw != 1) return false;
    uint8_t dummy;
    int nr = i2c_bus_read_timeout(bus_id, addr, &dummy, 1);
    return (nr == 1);
}

bool mpr121_touch_init(void) {
    if (g_ready) return true;

    if (!i2c_bus_init_once((i2c_bus_id_t)MPR121_I2C_BUS_ID)) {
        return false;
    }

    i2c_inst_t *i2c = mpr121_get_i2c();
    if (!i2c) {
        return false;
    }

    uint8_t addr = (uint8_t) MPR121_I2C_ADDR;
    if (!mpr121_probe((i2c_bus_id_t)MPR121_I2C_BUS_ID, addr)) {
        printf("MPR121: no device at I2C addr 0x%02X (probe NACK)\n", (unsigned)addr);
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

    g_sensor.i2c_port = i2c;
    g_sensor.i2c_addr = addr;

    mpr121_init(i2c, addr, &g_sensor);
    mpr121_set_thresholds((uint8_t)MPR121_TOUCH_THRESHOLD, (uint8_t)MPR121_RELEASE_THRESHOLD, &g_sensor);

    ensure_touch_hashes();
    g_last_touched = 0;
    g_last_poll_ms = 0;
    g_ready = true;
    return true;
}

#define MPR121_FILTERED_DATA_MAX 1023.0f  /* 10-bit filtered data */

static void mpr121_read_and_push(void) {
    uint16_t touched;
    mpr121_touched(&touched, &g_sensor);

    for (unsigned i = 0; i < (unsigned) MPR121_NUM_ELECTRODES; i++) {
        uint16_t mask = (uint16_t)(1u << i);
        bool now = (touched & mask) != 0;
        bool prev = (g_last_touched & mask) != 0;
        if (now != prev)
            ctrl_push_hash_f(g_hash_touch[i], now ? 1.0f : 0.0f);
    }
    g_last_touched = touched;

    /* Push filtered data (0..1) as touchN_level for pressure/degree of touch */
    for (unsigned i = 0; i < (unsigned) MPR121_NUM_ELECTRODES; i++) {
        uint16_t raw;
        mpr121_filtered_data((uint8_t)i, &raw, &g_sensor);
        float level = (float)raw / MPR121_FILTERED_DATA_MAX;
        if (level > 1.0f) level = 1.0f;
        ctrl_push_hash_f(g_hash_touch_level[i], level);
    }
}

void mpr121_touch_task(void) {
    if (!g_ready) return;

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    bool do_read = g_irq_pending;

    if (!do_read && (now_ms - g_last_poll_ms >= MPR121_POLL_MS)) {
        /* Polling fallback: IRQ may be not connected; read periodically so OLED and Pd get updates. */
        g_last_poll_ms = now_ms;
        do_read = true;
    }

    if (do_read) {
        g_irq_pending = false;
        mpr121_read_and_push();
    }
}

#endif /* ENABLE_MPR121 */
