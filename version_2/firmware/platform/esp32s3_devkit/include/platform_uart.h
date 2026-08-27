#ifndef GEOPHYS_PLATFORM_UART_H
#define GEOPHYS_PLATFORM_UART_H

#include <stddef.h>
#include <stdint.h>

#include "fw_status.h"
#include "platform_gpio.h"

typedef struct platform_uart platform_uart_t;

typedef enum {
    PLATFORM_UART_PORT_0 = 0,
    PLATFORM_UART_PORT_1,
    PLATFORM_UART_PORT_2,
} platform_uart_port_t;

typedef enum {
    PLATFORM_UART_DATA_BITS_7 = 0,
    PLATFORM_UART_DATA_BITS_8,
} platform_uart_data_bits_t;

typedef enum {
    PLATFORM_UART_PARITY_NONE = 0,
    PLATFORM_UART_PARITY_EVEN,
    PLATFORM_UART_PARITY_ODD,
} platform_uart_parity_t;

typedef enum {
    PLATFORM_UART_STOP_BITS_1 = 0,
    PLATFORM_UART_STOP_BITS_2,
} platform_uart_stop_bits_t;

typedef struct {
    platform_uart_port_t port;
    platform_gpio_pin_t tx_pin;
    platform_gpio_pin_t rx_pin;
    uint32_t requested_baud_rate;
    size_t rx_buffer_size_bytes;
    platform_uart_data_bits_t data_bits;
    platform_uart_parity_t parity;
    platform_uart_stop_bits_t stop_bits;
} platform_uart_config_t;

/**
 * @brief Configure and install one ESP-IDF UART driver.
 *
 * Initialization may allocate memory. The TX ring buffer is intentionally
 * disabled so writes can enforce their caller-provided timeout instead of
 * blocking inside uart_write_bytes().
 */
fw_status_t platform_uart_initialize(const platform_uart_config_t *config,
                                     platform_uart_t **uart);

/**
 * @brief Wait for accepted TX bytes to leave the UART, then release it.
 *
 * A timeout leaves the UART initialized so the owner may retry or recover.
 */
fw_status_t platform_uart_deinitialize(platform_uart_t *uart,
                                       uint32_t timeout_us);

/**
 * @brief Read at least one byte, then return all bytes already available.
 *
 * A successful call may return fewer than capacity_bytes. TIMEOUT means no
 * byte became available before the deadline. This operation is task-context
 * only and has no protocol-frame semantics.
 */
fw_status_t platform_uart_read_some(platform_uart_t *uart,
                                    uint8_t *data,
                                    size_t capacity_bytes,
                                    size_t *bytes_read,
                                    uint32_t timeout_us);

/**
 * @brief Push as many bytes as currently fit in the hardware TX FIFO.
 *
 * A successful call may accept fewer than length_bytes. The accepted bytes
 * may still be shifting out when this function returns. If the FIFO is full,
 * the call waits only until the supplied deadline for space to become
 * available.
 */
fw_status_t platform_uart_write_some(platform_uart_t *uart,
                                     const uint8_t *data,
                                     size_t length_bytes,
                                     size_t *bytes_written,
                                     uint32_t timeout_us);

fw_status_t platform_uart_wait_tx_idle(platform_uart_t *uart,
                                       uint32_t timeout_us);

fw_status_t platform_uart_get_baud_rate(const platform_uart_t *uart,
                                        uint32_t *requested_baud_rate,
                                        uint32_t *actual_baud_rate);

#endif /* GEOPHYS_PLATFORM_UART_H */
