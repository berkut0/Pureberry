#include "drv/i2c_bus.h"

#include "config.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

typedef struct {
    bool initialized;
    bool blocking_active;
#ifdef ENABLE_I2C_DMA
    bool dma_ready;
    i2c_dma_t dma;
#endif
} i2c_bus_state_t;

static const i2c_bus_config_t g_bus_config[I2C_BUS_ID_COUNT] = {
    {
        .inst = I2C_GET_INSTANCE(I2C_BUS0_INSTANCE),
        .sda_pin = I2C_BUS0_SDA_PIN,
        .scl_pin = I2C_BUS0_SCL_PIN,
        .baud_hz = I2C_BUS0_BAUD,
        .timeout_us = I2C_BUS0_TIMEOUT_US,
    },
    {
        .inst = I2C_GET_INSTANCE(I2C_BUS1_INSTANCE),
        .sda_pin = I2C_BUS1_SDA_PIN,
        .scl_pin = I2C_BUS1_SCL_PIN,
        .baud_hz = I2C_BUS1_BAUD,
        .timeout_us = I2C_BUS1_TIMEOUT_US,
    },
};

static i2c_bus_state_t g_bus_state[I2C_BUS_ID_COUNT];

static bool i2c_bus_valid_id(i2c_bus_id_t id) {
    return (id >= I2C_BUS_ID_0 && id < I2C_BUS_ID_COUNT);
}

const i2c_bus_config_t *i2c_bus_get_config(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) return NULL;
    return &g_bus_config[id];
}

bool i2c_bus_init_once(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) return false;
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (bus->initialized) return true;

    const i2c_bus_config_t *cfg = &g_bus_config[id];
    if (!cfg->inst) return false;

    gpio_set_function(cfg->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(cfg->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(cfg->sda_pin);
    gpio_pull_up(cfg->scl_pin);

    i2c_init(cfg->inst, cfg->baud_hz);
    bus->initialized = true;
    return true;
}

i2c_inst_t *i2c_bus_get_inst(i2c_bus_id_t id) {
    const i2c_bus_config_t *cfg = i2c_bus_get_config(id);
    return cfg ? cfg->inst : NULL;
}

#ifdef ENABLE_I2C_DMA
static bool i2c_bus_wait_for_idle(i2c_bus_state_t *bus, uint32_t timeout_us) {
    if (!bus->dma_ready) return true;
    absolute_time_t deadline = make_timeout_time_us(timeout_us);
    while (i2c_dma_busy(&bus->dma)) {
        i2c_dma_poll(&bus->dma);
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) {
            return false;
        }
        tight_loop_contents();
    }
    return true;
}
#else
static bool i2c_bus_wait_for_idle(i2c_bus_state_t *bus, uint32_t timeout_us) {
    (void)bus;
    (void)timeout_us;
    return true;
}
#endif

int i2c_bus_write_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop) {
    if (!i2c_bus_valid_id(id)) return PICO_ERROR_GENERIC;
    const i2c_bus_config_t *cfg = &g_bus_config[id];
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->initialized) return PICO_ERROR_GENERIC;

    if (!i2c_bus_wait_for_idle(bus, cfg->timeout_us)) {
        return PICO_ERROR_TIMEOUT;
    }

    bus->blocking_active = true;
    int res = i2c_write_timeout_us(cfg->inst, addr7, buf, len, nostop, cfg->timeout_us);
    bus->blocking_active = false;
    return res;
}

int i2c_bus_read_timeout(i2c_bus_id_t id, uint8_t addr7, uint8_t *buf, size_t len) {
    if (!i2c_bus_valid_id(id)) return PICO_ERROR_GENERIC;
    const i2c_bus_config_t *cfg = &g_bus_config[id];
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->initialized) return PICO_ERROR_GENERIC;

    if (!i2c_bus_wait_for_idle(bus, cfg->timeout_us)) {
        return PICO_ERROR_TIMEOUT;
    }

    bus->blocking_active = true;
    int res = i2c_read_timeout_us(cfg->inst, addr7, buf, len, false, cfg->timeout_us);
    bus->blocking_active = false;
    return res;
}

int i2c_bus_write_read_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len) {
    if (!i2c_bus_valid_id(id)) return PICO_ERROR_GENERIC;
    const i2c_bus_config_t *cfg = &g_bus_config[id];
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->initialized) return PICO_ERROR_GENERIC;

    if (!i2c_bus_wait_for_idle(bus, cfg->timeout_us)) {
        return PICO_ERROR_TIMEOUT;
    }

    bus->blocking_active = true;
    int nw = i2c_write_timeout_us(cfg->inst, addr7, tx, tx_len, true, cfg->timeout_us);
    if (nw != (int)tx_len) {
        bus->blocking_active = false;
        return nw;
    }
    int nr = i2c_read_timeout_us(cfg->inst, addr7, rx, rx_len, false, cfg->timeout_us);
    bus->blocking_active = false;
    if (nr != (int)rx_len) return nr;
    return (int)(tx_len + rx_len);
}

void i2c_bus_recover(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) return;
    const i2c_bus_config_t *cfg = &g_bus_config[id];
    i2c_bus_state_t *bus = &g_bus_state[id];
    bool restore_i2c = bus->initialized;

    gpio_init(cfg->sda_pin);
    gpio_init(cfg->scl_pin);
    gpio_pull_up(cfg->sda_pin);
    gpio_pull_up(cfg->scl_pin);

    gpio_set_dir(cfg->sda_pin, GPIO_IN);
    gpio_set_dir(cfg->scl_pin, GPIO_IN);
    sleep_us(5);

    if (gpio_get(cfg->sda_pin) == 0) {
        gpio_set_dir(cfg->scl_pin, GPIO_OUT);
        for (int i = 0; i < 9; i++) {
            gpio_put(cfg->scl_pin, 0);
            sleep_us(5);
            gpio_put(cfg->scl_pin, 1);
            sleep_us(5);
            if (gpio_get(cfg->sda_pin) != 0) break;
        }

        gpio_set_dir(cfg->sda_pin, GPIO_OUT);
        gpio_put(cfg->sda_pin, 0);
        sleep_us(5);
        gpio_put(cfg->scl_pin, 1);
        sleep_us(5);
        gpio_put(cfg->sda_pin, 1);
        sleep_us(5);
    }

    gpio_set_dir(cfg->sda_pin, GPIO_IN);
    gpio_set_dir(cfg->scl_pin, GPIO_IN);

    if (restore_i2c) {
        gpio_set_function(cfg->sda_pin, GPIO_FUNC_I2C);
        gpio_set_function(cfg->scl_pin, GPIO_FUNC_I2C);
        gpio_pull_up(cfg->sda_pin);
        gpio_pull_up(cfg->scl_pin);
    }
}

void i2c_bus_poll(void) {
#ifdef ENABLE_I2C_DMA
    for (int id = 0; id < I2C_BUS_ID_COUNT; id++) {
        i2c_bus_state_t *bus = &g_bus_state[id];
        if (bus->dma_ready) {
            i2c_dma_poll(&bus->dma);
        }
    }
#endif
}

#ifdef ENABLE_I2C_DMA
bool i2c_bus_dma_init(i2c_bus_id_t id, int dma_chan, uint8_t dma_irq_index) {
    if (!i2c_bus_valid_id(id)) return false;
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->initialized) return false;
    if (bus->dma_ready) return true;

    i2c_dma_init(&bus->dma, g_bus_config[id].inst, dma_chan, dma_irq_index);
    bus->dma_ready = (bus->dma.dma_chan >= 0);
    return bus->dma_ready;
}

bool i2c_bus_dma_ready(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) return false;
    return g_bus_state[id].dma_ready;
}

bool i2c_bus_dma_busy(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) return false;
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->dma_ready) return false;
    return i2c_dma_busy(&bus->dma);
}

bool i2c_bus_dma_submit(i2c_bus_id_t id, const i2c_dma_txn_t *txn) {
    if (!i2c_bus_valid_id(id)) return false;
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->dma_ready) return false;
    if (bus->blocking_active) return false;
    return i2c_dma_submit(&bus->dma, txn);
}
#endif
