#ifndef GEOPHYS_FW_SPI_H
#define GEOPHYS_FW_SPI_H

#include <stddef.h>
#include <stdint.h>

#include "fw_status.h"

/**
 * @brief Portable task-context full-duplex SPI transfer callback.
 *
 * The operation returns only after the transfer has completed or failed. A
 * NULL transmit buffer sends the configured filler byte. A NULL receive buffer
 * discards received bytes. timeout_us is an upper bound for waiting and must be
 * nonzero.
 *
 * This callback is not ISR-safe.
 */
typedef fw_status_t (*fw_spi_transfer_callback_t)(
    void *context,
    const uint8_t *tx_data,
    uint8_t *rx_data,
    size_t length_bytes,
    uint32_t timeout_us);

typedef struct {
    fw_spi_transfer_callback_t transfer;
    void *context;
} fw_spi_interface_t;

#endif /* GEOPHYS_FW_SPI_H */
