#ifndef GEOPHYS_FW_SPI_H
#define GEOPHYS_FW_SPI_H

#include <stddef.h>
#include <stdint.h>

#include "fw_error.h"

/**
 * @brief Portable task-context full-duplex SPI transfer callback.
 *
 * The operation returns only after the transfer has completed or failed. A
 * NULL transmit buffer sends the configured filler byte. A NULL receive buffer
 * discards received bytes. timeout_us is an upper bound for waiting and must be
 * nonzero. error is an optional portable failure-context output.
 *
 * This callback is not ISR-safe.
 */
typedef fw_status_t (*fw_spi_transfer_callback_t)(
    void *context,
    const uint8_t *tx_data,
    uint8_t *rx_data,
    size_t length_bytes,
    uint32_t timeout_us,
    fw_error_context_t *error);

/**
 * @brief Prepare a fixed-size burst session with hardware-framed chip select.
 *
 * The implementation reserves the bus and all transfer buffers once. No SPI
 * clocks are generated and chip select remains inactive until burst_trigger is
 * called. pattern is expanded once into a persistent transmit buffer and
 * repeated for every burst.
 */
typedef fw_status_t (*fw_spi_burst_prepare_callback_t)(
    void *context,
    const uint8_t *pattern,
    size_t pattern_length_bytes,
    size_t burst_length_bytes,
    uint32_t timeout_us,
    fw_error_context_t *error);

/**
 * @brief Finish the previous burst, if any, then start one new burst.
 *
 * On the first call, completed_rx_data is NULL and completed_length_bytes is
 * zero. Later calls return the preceding burst without copying it, then start
 * the new burst in a different fixed buffer. Returned data remains valid until
 * the next trigger or finish call and must be consumed before then.
 */
typedef fw_status_t (*fw_spi_burst_trigger_callback_t)(
    void *context,
    const uint8_t **completed_rx_data,
    size_t *completed_length_bytes,
    uint32_t timeout_us,
    fw_error_context_t *error);

/**
 * @brief Finish the final burst and release the bus.
 *
 * If a burst was active, its zero-copy buffer is returned to the caller.
 */
typedef fw_status_t (*fw_spi_burst_finish_callback_t)(
    void *context,
    const uint8_t **completed_rx_data,
    size_t *completed_length_bytes,
    uint32_t timeout_us,
    fw_error_context_t *error);

typedef struct {
    fw_spi_transfer_callback_t transfer;
    fw_spi_burst_prepare_callback_t burst_prepare;
    fw_spi_burst_trigger_callback_t burst_trigger;
    fw_spi_burst_finish_callback_t burst_finish;
    void *context;
} fw_spi_interface_t;

#endif /* GEOPHYS_FW_SPI_H */
