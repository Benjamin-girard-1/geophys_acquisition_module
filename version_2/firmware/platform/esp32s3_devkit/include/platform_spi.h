#ifndef GEOPHYS_PLATFORM_SPI_H
#define GEOPHYS_PLATFORM_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_spi.h"
#include "platform_gpio.h"

#define PLATFORM_SPI_PIN_UNUSED UINT32_MAX

typedef struct platform_spi_bus platform_spi_bus_t;
typedef struct platform_spi_device platform_spi_device_t;

typedef enum {
    PLATFORM_SPI_HOST_2 = 0,
    PLATFORM_SPI_HOST_3,
} platform_spi_host_t;

typedef enum {
    PLATFORM_SPI_BIT_ORDER_MSB_FIRST = 0,
    PLATFORM_SPI_BIT_ORDER_LSB_FIRST,
} platform_spi_bit_order_t;

typedef enum {
    PLATFORM_SPI_CS_ACTIVE_LOW = 0,
    PLATFORM_SPI_CS_ACTIVE_HIGH,
} platform_spi_cs_polarity_t;

typedef struct {
    platform_spi_host_t host;
    platform_gpio_pin_t clock_pin;
    platform_gpio_pin_t mosi_pin;
    platform_gpio_pin_t miso_pin;
    size_t maximum_transfer_size_bytes;
    bool dma_enabled;
} platform_spi_bus_config_t;

typedef struct {
    platform_gpio_pin_t chip_select_pin;
    platform_spi_cs_polarity_t chip_select_polarity;
    platform_spi_bit_order_t bit_order;
    uint8_t mode;
    uint8_t filler_byte;
    uint32_t initial_clock_hz;
    uint32_t maximum_clock_hz;
    uint32_t input_delay_ns;
    uint32_t chip_select_setup_us;
    uint32_t chip_select_hold_us;
    size_t maximum_transfer_size_bytes;
} platform_spi_device_config_t;

/**
 * @brief Initialize one ESP32 general-purpose SPI controller.
 *
 * Initialization may allocate memory. No allocation occurs in transfers.
 */
fw_status_t platform_spi_bus_initialize(
    const platform_spi_bus_config_t *config,
    platform_spi_bus_t **bus,
    fw_error_context_t *error);

/**
 * @brief Release an empty bus after any outstanding DMA result is drained.
 */
fw_status_t platform_spi_bus_deinitialize(platform_spi_bus_t *bus,
                                          uint32_t timeout_us,
                                          fw_error_context_t *error);

/**
 * @brief Add a manually selected SPI device and allocate its fixed buffers.
 */
fw_status_t platform_spi_device_add(
    platform_spi_bus_t *bus,
    const platform_spi_device_config_t *config,
    platform_spi_device_t **device,
    fw_error_context_t *error);

fw_status_t platform_spi_device_remove(platform_spi_device_t *device,
                                       uint32_t timeout_us,
                                       fw_error_context_t *error);

/**
 * @brief Synchronous transfer matching fw_spi_transfer_callback_t.
 *
 * The transfer is DMA-backed when DMA was enabled for the bus. The bus is
 * locked from chip-select assertion through deassertion. Only this task
 * blocks; the scheduler and other buses continue to run.
 */
fw_status_t platform_spi_transfer(void *context,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t length_bytes,
                                  uint32_t timeout_us,
                                  fw_error_context_t *error);

/**
 * @brief Change a device clock while it has no active transfer.
 *
 * ESP-IDF applies the requested frequency with a CS-inactive maintenance
 * transaction. The achieved frequency is returned because hardware dividers
 * may round the request.
 */
fw_status_t platform_spi_device_set_clock(platform_spi_device_t *device,
                                          uint32_t requested_clock_hz,
                                          uint32_t timeout_us,
                                          uint32_t *actual_clock_hz,
                                          fw_error_context_t *error);

fw_status_t platform_spi_device_get_clock(
    const platform_spi_device_t *device,
    uint32_t *requested_clock_hz,
    uint32_t *actual_clock_hz,
    fw_error_context_t *error);

fw_spi_interface_t platform_spi_device_interface(
    platform_spi_device_t *device);

#endif /* GEOPHYS_PLATFORM_SPI_H */
