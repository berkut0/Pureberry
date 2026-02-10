#include "drv/spi_bus.h"

#include "config.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

typedef struct {
    bool initialized;
} spi_bus_state_t;

static const spi_bus_config_t g_spi_config[SPI_BUS_ID_COUNT] = {
    {
        .inst = SPI_GET_INSTANCE(SPI_BUS0_INSTANCE),
        .sck_pin = SPI_BUS0_SCK_PIN,
        .tx_pin = SPI_BUS0_TX_PIN,
        .rx_pin = SPI_BUS0_RX_PIN,
        .baud_hz = SPI_BUS0_BAUD,
    },
    {
        .inst = SPI_GET_INSTANCE(SPI_BUS1_INSTANCE),
        .sck_pin = SPI_BUS1_SCK_PIN,
        .tx_pin = SPI_BUS1_TX_PIN,
        .rx_pin = SPI_BUS1_RX_PIN,
        .baud_hz = SPI_BUS1_BAUD,
    },
};

static spi_bus_state_t g_spi_state[SPI_BUS_ID_COUNT];

static bool spi_bus_valid_id(spi_bus_id_t id) {
    return (id >= SPI_BUS_ID_0 && id < SPI_BUS_ID_COUNT);
}

const spi_bus_config_t *spi_bus_get_config(spi_bus_id_t id) {
    if (!spi_bus_valid_id(id)) return NULL;
    return &g_spi_config[id];
}

bool spi_bus_init_once(spi_bus_id_t id) {
    if (!spi_bus_valid_id(id)) return false;
    spi_bus_state_t *bus = &g_spi_state[id];
    if (bus->initialized) return true;

    const spi_bus_config_t *cfg = &g_spi_config[id];
    if (!cfg->inst) return false;

    gpio_set_function(cfg->sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(cfg->tx_pin, GPIO_FUNC_SPI);
    gpio_set_function(cfg->rx_pin, GPIO_FUNC_SPI);

    spi_init(cfg->inst, cfg->baud_hz);
    bus->initialized = true;
    return true;
}

spi_inst_t *spi_bus_get_inst(spi_bus_id_t id) {
    const spi_bus_config_t *cfg = spi_bus_get_config(id);
    return cfg ? cfg->inst : NULL;
}
