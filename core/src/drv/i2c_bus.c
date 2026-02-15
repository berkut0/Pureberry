#include "drv/i2c_bus.h"

#include "config.h"

#include "drv/i2c_dma.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/regs/i2c.h"
#include "pico/error.h"
#include "pico/stdlib.h"

#ifndef I2C_BUS_ASYNC_MAX_CMD_WORDS
// Must cover at least one full SSD1306 frame burst (1025 bytes) with small headroom.
#define I2C_BUS_ASYNC_MAX_CMD_WORDS 1152u
#endif

typedef struct {
    bool initialized;
    bool blocking_active;
    bool dma_ready;
    bool async_inflight;
    i2c_dma_t dma;
    i2c_bus_done_cb_t async_done;
    void *async_user;
    uint32_t async_cmd_buf[I2C_BUS_ASYNC_MAX_CMD_WORDS];
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

static i2c_bus_result_t i2c_bus_check_addr7(uint8_t addr7) {
    if (addr7 >= 0x80u) {
        return I2C_BUS_RESULT_EINVAL;
    }
    return I2C_BUS_RESULT_OK;
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

static i2c_bus_result_t i2c_bus_map_dma_result(i2c_dma_result_t result) {
    switch (result) {
        case I2C_DMA_RESULT_OK:
            return I2C_BUS_RESULT_OK;
        case I2C_DMA_RESULT_EINVAL:
            return I2C_BUS_RESULT_EINVAL;
        case I2C_DMA_RESULT_EQUEUE_FULL:
            return I2C_BUS_RESULT_EBUSY;
        case I2C_DMA_RESULT_ETIMEOUT:
            return I2C_BUS_RESULT_ETIMEOUT;
        case I2C_DMA_RESULT_EABORT:
        default:
            return I2C_BUS_RESULT_EIO;
    }
}

static uint32_t i2c_bus_effective_timeout_us(const i2c_bus_config_t *cfg, uint32_t timeout_us) {
    if (timeout_us > 0u) {
        return timeout_us;
    }
    return cfg ? cfg->timeout_us : 0u;
}

static uint32_t i2c_bus_effective_idle_timeout_us(const i2c_bus_config_t *cfg) {
    uint32_t timeout_us = cfg ? cfg->timeout_us : 0u;
    if (timeout_us < (uint32_t)I2C_BUS_DMA_IDLE_TIMEOUT_US) {
        timeout_us = (uint32_t)I2C_BUS_DMA_IDLE_TIMEOUT_US;
    }
    return timeout_us;
}

static void i2c_bus_clear_async_slot(i2c_bus_state_t *bus) {
    bus->async_done = NULL;
    bus->async_user = NULL;
    bus->async_inflight = false;
}

static void i2c_bus_dma_done(void *user, i2c_dma_result_t result) {
    i2c_bus_state_t *bus = (i2c_bus_state_t *)user;
    if (!bus) return;

    i2c_bus_done_cb_t done = bus->async_done;
    void *done_user = bus->async_user;
    i2c_bus_clear_async_slot(bus);

    if (done) {
        done(done_user, i2c_bus_map_dma_result(result));
    }
}

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

static i2c_bus_result_t i2c_bus_build_read_cmds(uint32_t *out_cmds, size_t out_cap, size_t rx_len) {
    if (!out_cmds || rx_len == 0u || out_cap < rx_len) {
        return I2C_BUS_RESULT_EINVAL;
    }

    for (size_t i = 0u; i < rx_len; i++) {
        uint32_t cmd = I2C_IC_DATA_CMD_CMD_BITS;
        if (i == (rx_len - 1u)) {
            cmd |= I2C_IC_DATA_CMD_STOP_BITS;
        }
        out_cmds[i] = cmd;
    }
    return I2C_BUS_RESULT_OK;
}

static i2c_bus_result_t i2c_bus_build_write_read_cmds(
    uint32_t *out_cmds,
    size_t out_cap,
    const uint8_t *tx,
    size_t tx_len,
    size_t rx_len
) {
    size_t total = tx_len + rx_len;
    if (!out_cmds || !tx || tx_len == 0u || rx_len == 0u || out_cap < total) {
        return I2C_BUS_RESULT_EINVAL;
    }

    for (size_t i = 0u; i < tx_len; i++) {
        out_cmds[i] = (uint32_t)tx[i] & 0xFFu;
    }
    for (size_t i = 0u; i < rx_len; i++) {
        uint32_t cmd = I2C_IC_DATA_CMD_CMD_BITS;
        if (i == 0u) {
            cmd |= I2C_IC_DATA_CMD_RESTART_BITS;
        }
        if (i == (rx_len - 1u)) {
            cmd |= I2C_IC_DATA_CMD_STOP_BITS;
        }
        out_cmds[tx_len + i] = cmd;
    }
    return I2C_BUS_RESULT_OK;
}

static i2c_bus_result_t i2c_bus_submit_async(
    i2c_bus_id_t id,
    uint8_t addr7,
    const uint32_t *cmds,
    size_t cmd_count,
    uint8_t *rx,
    size_t rx_len,
    uint32_t timeout_us,
    i2c_bus_done_cb_t done,
    void *user
) {
    i2c_bus_state_t *bus = &g_bus_state[id];
    if (!bus->dma_ready) {
        return I2C_BUS_RESULT_EIO;
    }
    if (bus->blocking_active || bus->async_inflight || i2c_dma_busy(&bus->dma)) {
        return I2C_BUS_RESULT_EBUSY;
    }

    const i2c_bus_config_t *cfg = &g_bus_config[id];
    i2c_dma_txn_t txn = {
        .addr7 = addr7,
        .cmds = cmds,
        .cmd_count = cmd_count,
        .timeout_us = i2c_bus_effective_timeout_us(cfg, timeout_us),
        .rx = rx,
        .rx_count = rx_len,
        .done = i2c_bus_dma_done,
        .user = bus,
    };

    bus->async_done = done;
    bus->async_user = user;
    bus->async_inflight = true;

    if (!i2c_dma_submit(&bus->dma, &txn)) {
        i2c_bus_clear_async_slot(bus);
        return I2C_BUS_RESULT_EBUSY;
    }

    return I2C_BUS_RESULT_OK;
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
    i2c_dma_init(&bus->dma, cfg->inst, -1, 1);
    bus->dma_ready = (bus->dma.dma_chan >= 0);
    bus->initialized = true;
    return true;
}

i2c_bus_result_t i2c_bus_write(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop) {
    i2c_bus_result_t ready = i2c_bus_check_ready(id);
    if (ready != I2C_BUS_RESULT_OK) {
        return ready;
    }
    i2c_bus_result_t addr_ok = i2c_bus_check_addr7(addr7);
    if (addr_ok != I2C_BUS_RESULT_OK) {
        return addr_ok;
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
    i2c_bus_result_t addr_ok = i2c_bus_check_addr7(addr7);
    if (addr_ok != I2C_BUS_RESULT_OK) {
        return addr_ok;
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
    i2c_bus_result_t addr_ok = i2c_bus_check_addr7(addr7);
    if (addr_ok != I2C_BUS_RESULT_OK) {
        return addr_ok;
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

i2c_bus_result_t i2c_bus_write_async(
    i2c_bus_id_t id,
    uint8_t addr7,
    const uint8_t *tx,
    size_t tx_len,
    uint32_t timeout_us,
    i2c_bus_done_cb_t done,
    void *user
) {
    i2c_bus_result_t ready = i2c_bus_check_ready(id);
    if (ready != I2C_BUS_RESULT_OK) {
        return ready;
    }
    i2c_bus_result_t addr_ok = i2c_bus_check_addr7(addr7);
    if (addr_ok != I2C_BUS_RESULT_OK) {
        return addr_ok;
    }
    i2c_bus_result_t tx_args = i2c_bus_check_transfer_args(tx, tx_len);
    if (tx_args != I2C_BUS_RESULT_OK) {
        return tx_args;
    }
    if (tx_len > I2C_BUS_ASYNC_MAX_CMD_WORDS) {
        return I2C_BUS_RESULT_EINVAL;
    }

    size_t built = i2c_dma_build_write_cmds(
        g_bus_state[id].async_cmd_buf,
        I2C_BUS_ASYNC_MAX_CMD_WORDS,
        tx,
        tx_len,
        false,
        true
    );
    if (built != tx_len) {
        return I2C_BUS_RESULT_EINVAL;
    }

    return i2c_bus_submit_async(id, addr7, g_bus_state[id].async_cmd_buf, tx_len, NULL, 0u, timeout_us, done, user);
}

i2c_bus_result_t i2c_bus_read_async(
    i2c_bus_id_t id,
    uint8_t addr7,
    uint8_t *rx,
    size_t rx_len,
    uint32_t timeout_us,
    i2c_bus_done_cb_t done,
    void *user
) {
    i2c_bus_result_t ready = i2c_bus_check_ready(id);
    if (ready != I2C_BUS_RESULT_OK) {
        return ready;
    }
    i2c_bus_result_t addr_ok = i2c_bus_check_addr7(addr7);
    if (addr_ok != I2C_BUS_RESULT_OK) {
        return addr_ok;
    }
    i2c_bus_result_t rx_args = i2c_bus_check_transfer_args(rx, rx_len);
    if (rx_args != I2C_BUS_RESULT_OK) {
        return rx_args;
    }
    if (rx_len > I2C_BUS_ASYNC_MAX_CMD_WORDS) {
        return I2C_BUS_RESULT_EINVAL;
    }

    i2c_bus_result_t build = i2c_bus_build_read_cmds(g_bus_state[id].async_cmd_buf, I2C_BUS_ASYNC_MAX_CMD_WORDS, rx_len);
    if (build != I2C_BUS_RESULT_OK) {
        return build;
    }

    return i2c_bus_submit_async(id, addr7, g_bus_state[id].async_cmd_buf, rx_len, rx, rx_len, timeout_us, done, user);
}

i2c_bus_result_t i2c_bus_write_read_async(
    i2c_bus_id_t id,
    uint8_t addr7,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len,
    uint32_t timeout_us,
    i2c_bus_done_cb_t done,
    void *user
) {
    i2c_bus_result_t ready = i2c_bus_check_ready(id);
    if (ready != I2C_BUS_RESULT_OK) {
        return ready;
    }
    i2c_bus_result_t addr_ok = i2c_bus_check_addr7(addr7);
    if (addr_ok != I2C_BUS_RESULT_OK) {
        return addr_ok;
    }
    i2c_bus_result_t tx_args = i2c_bus_check_transfer_args(tx, tx_len);
    if (tx_args != I2C_BUS_RESULT_OK) {
        return tx_args;
    }
    i2c_bus_result_t rx_args = i2c_bus_check_transfer_args(rx, rx_len);
    if (rx_args != I2C_BUS_RESULT_OK) {
        return rx_args;
    }
    if (tx_len + rx_len > I2C_BUS_ASYNC_MAX_CMD_WORDS) {
        return I2C_BUS_RESULT_EINVAL;
    }

    i2c_bus_result_t build = i2c_bus_build_write_read_cmds(
        g_bus_state[id].async_cmd_buf,
        I2C_BUS_ASYNC_MAX_CMD_WORDS,
        tx,
        tx_len,
        rx_len
    );
    if (build != I2C_BUS_RESULT_OK) {
        return build;
    }

    return i2c_bus_submit_async(
        id,
        addr7,
        g_bus_state[id].async_cmd_buf,
        tx_len + rx_len,
        rx,
        rx_len,
        timeout_us,
        done,
        user
    );
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
    for (int id = 0; id < I2C_BUS_ID_COUNT; id++) {
        i2c_bus_state_t *bus = &g_bus_state[id];
        if (bus->dma_ready) {
            i2c_dma_poll(&bus->dma);
        }
    }
}
