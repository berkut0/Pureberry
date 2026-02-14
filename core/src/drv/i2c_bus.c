#include "drv/i2c_bus.h"

#include "config.h"

#include "pico/error.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#ifdef ENABLE_I2C_DMA
#include "drv/i2c_dma.h"
#endif

typedef struct {
    bool initialized;
    bool blocking_active;
#ifdef ENABLE_I2C_DMA
    bool dma_ready;
    i2c_dma_t dma;
#endif
} i2c_bus_state_t;

typedef struct {
    i2c_inst_t *inst;
    uint sda_pin;
    uint scl_pin;
    uint32_t baud_hz;
    uint32_t timeout_us;
} i2c_bus_config_t;

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

uint32_t i2c_bus_get_baud_hz(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) return 0u;
    return g_bus_config[id].baud_hz;
}

static i2c_bus_result_t i2c_bus_check_transfer_args(const uint8_t *buf, size_t len) {
    if (!buf || len == 0u) {
        return I2C_BUS_RESULT_EINVAL;
    }
    return I2C_BUS_RESULT_OK;
}

static i2c_bus_result_t i2c_bus_check_ready(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) {
        return I2C_BUS_RESULT_EINVAL;
    }
    if (!g_bus_state[id].initialized) {
        return I2C_BUS_RESULT_ENOT_INIT;
    }
    return I2C_BUS_RESULT_OK;
}

static i2c_bus_result_t i2c_bus_map_transfer_result(int actual, size_t expected_len) {
    if (actual == (int)expected_len) {
        return I2C_BUS_RESULT_OK;
    }
    if (actual == PICO_ERROR_TIMEOUT) {
        return I2C_BUS_RESULT_ETIMEOUT;
    }
    return I2C_BUS_RESULT_EIO;
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

static uint32_t i2c_bus_effective_idle_timeout_us(const i2c_bus_config_t *cfg) {
    uint32_t timeout_us = cfg ? cfg->timeout_us : 0u;
#ifdef ENABLE_I2C_DMA
    if (timeout_us < (uint32_t)I2C_BUS_DMA_IDLE_TIMEOUT_US) {
        timeout_us = (uint32_t)I2C_BUS_DMA_IDLE_TIMEOUT_US;
    }
#endif
    return timeout_us;
}

i2c_bus_result_t i2c_bus_write(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop) {
    i2c_bus_result_t ready = i2c_bus_check_ready(id);
    if (ready != I2C_BUS_RESULT_OK) {
        return ready;
    }
    i2c_bus_result_t args = i2c_bus_check_transfer_args(buf, len);
    if (args != I2C_BUS_RESULT_OK) {
        return args;
    }
    int written = i2c_bus_write_timeout(id, addr7, buf, len, nostop);
    return i2c_bus_map_transfer_result(written, len);
}

i2c_bus_result_t i2c_bus_read(i2c_bus_id_t id, uint8_t addr7, uint8_t *buf, size_t len) {
    i2c_bus_result_t ready = i2c_bus_check_ready(id);
    if (ready != I2C_BUS_RESULT_OK) {
        return ready;
    }
    i2c_bus_result_t args = i2c_bus_check_transfer_args(buf, len);
    if (args != I2C_BUS_RESULT_OK) {
        return args;
    }
    int read = i2c_bus_read_timeout(id, addr7, buf, len);
    return i2c_bus_map_transfer_result(read, len);
}

i2c_bus_result_t i2c_bus_write_read(
    i2c_bus_id_t id,
    uint8_t addr7,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len
) {
    i2c_bus_result_t ready = i2c_bus_check_ready(id);
    if (ready != I2C_BUS_RESULT_OK) {
        return ready;
    }
    i2c_bus_result_t tx_args = i2c_bus_check_transfer_args(tx, tx_len);
    if (tx_args != I2C_BUS_RESULT_OK) {
        return tx_args;
    }
    i2c_bus_result_t rx_args = i2c_bus_check_transfer_args(rx, rx_len);
    if (rx_args != I2C_BUS_RESULT_OK) {
        return rx_args;
    }
    int transferred = i2c_bus_write_read_timeout(id, addr7, tx, tx_len, rx, rx_len);
    return i2c_bus_map_transfer_result(transferred, tx_len + rx_len);
}

int i2c_bus_write_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop) {
    if (!i2c_bus_valid_id(id)) return PICO_ERROR_GENERIC;
    const i2c_bus_config_t *cfg = &g_bus_config[id];
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->initialized) return PICO_ERROR_GENERIC;

    if (!i2c_bus_wait_for_idle(bus, i2c_bus_effective_idle_timeout_us(cfg))) {
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

    if (!i2c_bus_wait_for_idle(bus, i2c_bus_effective_idle_timeout_us(cfg))) {
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

    if (!i2c_bus_wait_for_idle(bus, i2c_bus_effective_idle_timeout_us(cfg))) {
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
    if (nr != (int)rx_len) {
        return nr;
    }
    return (int)(tx_len + rx_len);
}

bool i2c_bus_recover(i2c_bus_id_t id) {
    if (!i2c_bus_valid_id(id)) return false;
    const i2c_bus_config_t *cfg = &g_bus_config[id];
    i2c_bus_state_t *bus = &g_bus_state[id];
    bool restore_i2c = bus->initialized;

    // Do not bit-bang recovery while a DMA transfer is still active.
    // This avoids corrupting an in-flight OLED transaction.
    if (!i2c_bus_wait_for_idle(bus, i2c_bus_effective_idle_timeout_us(cfg))) {
        return false;
    }

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
    return true;
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
i2c_bus_result_t i2c_bus_dma_init(i2c_bus_id_t id, int dma_chan, uint8_t dma_irq_index) {
    if (!i2c_bus_valid_id(id)) return I2C_BUS_RESULT_EINVAL;
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->initialized) return I2C_BUS_RESULT_ENOT_INIT;
    if (bus->dma_ready) return I2C_BUS_RESULT_OK;

    i2c_dma_init(&bus->dma, g_bus_config[id].inst, dma_chan, dma_irq_index);
    bus->dma_ready = (bus->dma.dma_chan >= 0);
    return bus->dma_ready ? I2C_BUS_RESULT_OK : I2C_BUS_RESULT_EIO;
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

static i2c_bus_result_t i2c_bus_dma_check_write_args(uint8_t addr7, const i2c_bus_dma_write_req_t *req) {
    if (!req) return I2C_BUS_RESULT_EINVAL;
    if (!req->bytes || req->len == 0u) return I2C_BUS_RESULT_EINVAL;
    if (!req->cmd_buf || req->cmd_buf_words < req->len) return I2C_BUS_RESULT_EINVAL;
    if (addr7 >= 0x80u) return I2C_BUS_RESULT_EINVAL;
    return I2C_BUS_RESULT_OK;
}

i2c_bus_result_t i2c_bus_dma_submit_writes(
    i2c_bus_id_t id,
    uint8_t addr7,
    const i2c_bus_dma_write_req_t *reqs,
    size_t req_count
) {
    if (!i2c_bus_valid_id(id)) return I2C_BUS_RESULT_EINVAL;
    if (!reqs || req_count == 0u) return I2C_BUS_RESULT_EINVAL;

    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->initialized || !bus->dma_ready) return I2C_BUS_RESULT_ENOT_INIT;
    if (bus->blocking_active) return I2C_BUS_RESULT_EBUSY;

    // Serialize as one sequence: require idle before queueing.
    if (i2c_dma_busy(&bus->dma)) return I2C_BUS_RESULT_EBUSY;

    // Validate and build all command buffers before first submit.
    for (size_t i = 0; i < req_count; i++) {
        const i2c_bus_dma_write_req_t *req = &reqs[i];
        i2c_bus_result_t args = i2c_bus_dma_check_write_args(addr7, req);
        if (args != I2C_BUS_RESULT_OK) return args;

        size_t built = i2c_dma_build_write_cmds(
            req->cmd_buf,
            req->cmd_buf_words,
            req->bytes,
            req->len,
            req->restart_first,
            true
        );
        if (built != req->len) return I2C_BUS_RESULT_EINVAL;
    }

    for (size_t i = 0; i < req_count; i++) {
        const i2c_bus_dma_write_req_t *req = &reqs[i];
        i2c_dma_txn_t txn = {
            .addr7 = addr7,
            .cmds = req->cmd_buf,
            .cmd_count = req->len,
            .timeout_us = req->timeout_us,
            .done = NULL,
            .user = NULL,
        };
        if (!i2c_dma_submit(&bus->dma, &txn)) {
            // Under non-reentrant contract after idle pre-check this should not fail.
            return (i == 0u) ? I2C_BUS_RESULT_EBUSY : I2C_BUS_RESULT_EIO;
        }
    }

    return I2C_BUS_RESULT_OK;
}
#endif
