#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "pico/types.h"
#include "hardware/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPI_BUS_ID_0 = 0,
    SPI_BUS_ID_1 = 1,
    SPI_BUS_ID_COUNT
} spi_bus_id_t;

typedef struct {
    spi_inst_t *inst;
    uint sck_pin;
    uint tx_pin;
    uint rx_pin;
    uint32_t baud_hz;
} spi_bus_config_t;

const spi_bus_config_t *spi_bus_get_config(spi_bus_id_t id);
bool spi_bus_init_once(spi_bus_id_t id);
spi_inst_t *spi_bus_get_inst(spi_bus_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* SPI_BUS_H */
