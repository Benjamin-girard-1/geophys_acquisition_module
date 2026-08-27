#include "transport_uart.h"

#include "platform_uart.h"

static fw_status_t read_some(void *context,
                             uint8_t *data,
                             size_t capacity_bytes,
                             size_t *bytes_read,
                             uint32_t timeout_us,
                             fw_error_context_t *error)
{
    return platform_uart_read_some(
        (platform_uart_t *)context,
        data,
        capacity_bytes,
        bytes_read,
        timeout_us,
        error);
}

static fw_status_t write_some(void *context,
                              const uint8_t *data,
                              size_t length_bytes,
                              size_t *bytes_written,
                              uint32_t timeout_us,
                              fw_error_context_t *error)
{
    return platform_uart_write_some(
        (platform_uart_t *)context,
        data,
        length_bytes,
        bytes_written,
        timeout_us,
        error);
}

transport_interface_t transport_uart_interface(struct platform_uart *uart)
{
    const transport_interface_t interface = {
        .read_some = read_some,
        .write_some = write_some,
        .context = uart,
    };
    return interface;
}
