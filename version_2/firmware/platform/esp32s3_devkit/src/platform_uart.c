#include "platform_uart.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "platform_error.h"

struct platform_uart {
    uart_port_t port;
    uint32_t requested_baud_rate;
    uint32_t actual_baud_rate;
    bool initialized;
};

static uint32_t uart_instance(const platform_uart_t *uart)
{
    return (uart == NULL) ? FW_ERROR_INSTANCE_NONE : (uint32_t)uart->port;
}

static uint32_t uart_instance_from_platform(platform_uart_port_t port)
{
    switch (port) {
    case PLATFORM_UART_PORT_0:
        return 0U;
    case PLATFORM_UART_PORT_1:
        return 1U;
    case PLATFORM_UART_PORT_2:
        return 2U;
    default:
        return FW_ERROR_INSTANCE_NONE;
    }
}

static uint32_t size_to_detail(size_t size)
{
    return (size > (size_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)size;
}

static bool port_to_idf(platform_uart_port_t port, uart_port_t *idf_port)
{
    switch (port) {
    case PLATFORM_UART_PORT_0:
        *idf_port = UART_NUM_0;
        return true;
    case PLATFORM_UART_PORT_1:
        *idf_port = UART_NUM_1;
        return true;
    case PLATFORM_UART_PORT_2:
        *idf_port = UART_NUM_2;
        return true;
    default:
        return false;
    }
}

static bool data_bits_to_idf(platform_uart_data_bits_t data_bits,
                             uart_word_length_t *idf_data_bits)
{
    switch (data_bits) {
    case PLATFORM_UART_DATA_BITS_7:
        *idf_data_bits = UART_DATA_7_BITS;
        return true;
    case PLATFORM_UART_DATA_BITS_8:
        *idf_data_bits = UART_DATA_8_BITS;
        return true;
    default:
        return false;
    }
}

static bool parity_to_idf(platform_uart_parity_t parity,
                          uart_parity_t *idf_parity)
{
    switch (parity) {
    case PLATFORM_UART_PARITY_NONE:
        *idf_parity = UART_PARITY_DISABLE;
        return true;
    case PLATFORM_UART_PARITY_EVEN:
        *idf_parity = UART_PARITY_EVEN;
        return true;
    case PLATFORM_UART_PARITY_ODD:
        *idf_parity = UART_PARITY_ODD;
        return true;
    default:
        return false;
    }
}

static bool stop_bits_to_idf(platform_uart_stop_bits_t stop_bits,
                             uart_stop_bits_t *idf_stop_bits)
{
    switch (stop_bits) {
    case PLATFORM_UART_STOP_BITS_1:
        *idf_stop_bits = UART_STOP_BITS_1;
        return true;
    case PLATFORM_UART_STOP_BITS_2:
        *idf_stop_bits = UART_STOP_BITS_2;
        return true;
    default:
        return false;
    }
}

static bool is_valid_input_pin(platform_gpio_pin_t pin)
{
    return (pin < (platform_gpio_pin_t)GPIO_NUM_MAX) &&
           GPIO_IS_VALID_GPIO((gpio_num_t)pin);
}

static bool is_valid_output_pin(platform_gpio_pin_t pin)
{
    return (pin < (platform_gpio_pin_t)GPIO_NUM_MAX) &&
           GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin);
}

static int64_t deadline_from_timeout(uint32_t timeout_us)
{
    return esp_timer_get_time() + (int64_t)timeout_us;
}

static TickType_t ticks_until(int64_t deadline_us)
{
    const int64_t remaining_us = deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        return 0;
    }

    const uint64_t ticks =
        (((uint64_t)remaining_us * (uint64_t)configTICK_RATE_HZ) +
         UINT64_C(999999)) /
        UINT64_C(1000000);

    if (ticks >= (uint64_t)portMAX_DELAY) {
        return portMAX_DELAY - 1U;
    }

    return (TickType_t)ticks;
}

static bool uart_is_ready(const platform_uart_t *uart)
{
    return (uart != NULL) && uart->initialized;
}

fw_status_t platform_uart_initialize(const platform_uart_config_t *config,
                                     platform_uart_t **uart,
                                     fw_error_context_t *error)
{
    uart_port_t idf_port;
    uart_word_length_t idf_data_bits;
    uart_parity_t idf_parity;
    uart_stop_bits_t idf_stop_bits;

    platform_error_clear(error);

    if ((config == NULL) || (uart == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_INITIALIZE,
            (config == NULL) ? FW_ERROR_INSTANCE_NONE :
            uart_instance_from_platform(config->port),
            (config == NULL) ? 0U : config->requested_baud_rate);
    }
    *uart = NULL;

    if (!port_to_idf(config->port, &idf_port) ||
        !data_bits_to_idf(config->data_bits, &idf_data_bits) ||
        !parity_to_idf(config->parity, &idf_parity) ||
        !stop_bits_to_idf(config->stop_bits, &idf_stop_bits) ||
        !is_valid_output_pin(config->tx_pin) ||
        !is_valid_input_pin(config->rx_pin) ||
        (config->requested_baud_rate == 0U) ||
        (config->requested_baud_rate > (uint32_t)INT_MAX) ||
        (config->rx_buffer_size_bytes > (size_t)INT_MAX) ||
        (config->rx_buffer_size_bytes <= (size_t)UART_HW_FIFO_LEN(idf_port))) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_INITIALIZE,
            uart_instance_from_platform(config->port),
            config->requested_baud_rate);
    }
    if (uart_is_driver_installed(idf_port)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_INITIALIZE,
            uart_instance_from_platform(config->port),
            config->requested_baud_rate);
    }

    platform_uart_t *new_uart = heap_caps_calloc(
        1U, sizeof(*new_uart), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (new_uart == NULL) {
        return platform_error_set(
            error, FW_STATUS_INTERNAL, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_INITIALIZE,
            uart_instance_from_platform(config->port),
            config->requested_baud_rate);
    }

    const uart_config_t idf_config = {
        .baud_rate = (int)config->requested_baud_rate,
        .data_bits = idf_data_bits,
        .parity = idf_parity,
        .stop_bits = idf_stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0U,
        .source_clk = UART_SCLK_DEFAULT,
    };

    fw_status_t status = platform_error_from_esp_err(
        uart_param_config(idf_port, &idf_config), error,
        FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_INITIALIZE,
        uart_instance_from_platform(config->port),
        config->requested_baud_rate);
    if (status != FW_STATUS_OK) {
        heap_caps_free(new_uart);
        return status;
    }

    status = platform_error_from_esp_err(uart_set_pin(
        idf_port,
        (int)config->tx_pin,
        (int)config->rx_pin,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE), error, FW_ERROR_RESOURCE_UART,
        FW_ERROR_OPERATION_INITIALIZE,
        uart_instance_from_platform(config->port),
        config->requested_baud_rate);
    if (status != FW_STATUS_OK) {
        heap_caps_free(new_uart);
        return status;
    }

    status = platform_error_from_esp_err(uart_driver_install(
        idf_port,
        (int)config->rx_buffer_size_bytes,
        0,
        0,
        NULL,
        0), error, FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_INITIALIZE,
        uart_instance_from_platform(config->port),
        config->requested_baud_rate);
    if (status != FW_STATUS_OK) {
        heap_caps_free(new_uart);
        return status;
    }

    uint32_t actual_baud_rate = 0U;
    status = platform_error_from_esp_err(
        uart_get_baudrate(idf_port, &actual_baud_rate), error,
        FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_INITIALIZE,
        uart_instance_from_platform(config->port),
        config->requested_baud_rate);
    if (status != FW_STATUS_OK) {
        (void)uart_driver_delete(idf_port);
        heap_caps_free(new_uart);
        return status;
    }

    new_uart->port = idf_port;
    new_uart->requested_baud_rate = config->requested_baud_rate;
    new_uart->actual_baud_rate = actual_baud_rate;
    new_uart->initialized = true;
    *uart = new_uart;
    return FW_STATUS_OK;
}

fw_status_t platform_uart_wait_tx_idle(platform_uart_t *uart,
                                       uint32_t timeout_us,
                                       fw_error_context_t *error)
{
    platform_error_clear(error);

    if (!uart_is_ready(uart)) {
        return platform_error_set(
            error,
            (uart == NULL) ? FW_STATUS_INVALID_ARGUMENT :
            FW_STATUS_NOT_INITIALIZED,
            FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_WAIT,
            uart_instance(uart), timeout_us);
    }
    if (timeout_us == 0U) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WAIT, uart_instance(uart), timeout_us);
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    const TickType_t wait_ticks = ticks_until(deadline_us);
    if (wait_ticks == 0U) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WAIT, uart_instance(uart), timeout_us);
    }

    const fw_status_t status = platform_error_from_esp_err(
        uart_wait_tx_done(uart->port, wait_ticks), error,
        FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_WAIT,
        uart_instance(uart), timeout_us);
    if (status != FW_STATUS_OK) {
        return status;
    }

    if (esp_timer_get_time() > deadline_us) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WAIT, uart_instance(uart), timeout_us);
    }
    return FW_STATUS_OK;
}

fw_status_t platform_uart_deinitialize(platform_uart_t *uart,
                                       uint32_t timeout_us,
                                       fw_error_context_t *error)
{
    platform_error_clear(error);

    if (!uart_is_ready(uart)) {
        return platform_error_set(
            error,
            (uart == NULL) ? FW_STATUS_INVALID_ARGUMENT :
            FW_STATUS_NOT_INITIALIZED,
            FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_DEINITIALIZE,
            uart_instance(uart), timeout_us);
    }

    fw_status_t status = platform_uart_wait_tx_idle(uart, timeout_us, NULL);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_DEINITIALIZE, uart_instance(uart), timeout_us);
    }

    status = platform_error_from_esp_err(
        uart_driver_delete(uart->port), error, FW_ERROR_RESOURCE_UART,
        FW_ERROR_OPERATION_DEINITIALIZE, uart_instance(uart), timeout_us);
    if (status != FW_STATUS_OK) {
        return status;
    }

    uart->initialized = false;
    heap_caps_free(uart);
    return FW_STATUS_OK;
}

fw_status_t platform_uart_read_some(platform_uart_t *uart,
                                    uint8_t *data,
                                    size_t capacity_bytes,
                                    size_t *bytes_read,
                                    uint32_t timeout_us,
                                    fw_error_context_t *error)
{
    platform_error_clear(error);

    if (bytes_read == NULL) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_READ, uart_instance(uart),
            size_to_detail(capacity_bytes));
    }
    *bytes_read = 0U;

    if (!uart_is_ready(uart)) {
        return platform_error_set(
            error,
            (uart == NULL) ? FW_STATUS_INVALID_ARGUMENT :
            FW_STATUS_NOT_INITIALIZED,
            FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_READ,
            uart_instance(uart), size_to_detail(capacity_bytes));
    }
    if ((data == NULL) || (capacity_bytes == 0U) ||
        (capacity_bytes > (size_t)UINT32_MAX) || (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_READ, uart_instance(uart),
            size_to_detail(capacity_bytes));
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    size_t buffered_bytes = 0U;
    fw_status_t status = platform_error_from_esp_err(
        uart_get_buffered_data_len(uart->port, &buffered_bytes), error,
        FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_READ,
        uart_instance(uart), size_to_detail(capacity_bytes));
    if (status != FW_STATUS_OK) {
        return status;
    }

    if (buffered_bytes == 0U) {
        const TickType_t wait_ticks = ticks_until(deadline_us);
        if (wait_ticks == 0U) {
            return platform_error_set(
                error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
                FW_ERROR_OPERATION_READ, uart_instance(uart),
                size_to_detail(capacity_bytes));
        }

        const int result = uart_read_bytes(
            uart->port, data, 1U, wait_ticks);
        if (result < 0) {
            return platform_error_set(
                error, FW_STATUS_IO, FW_ERROR_RESOURCE_UART,
                FW_ERROR_OPERATION_READ, uart_instance(uart),
                size_to_detail(capacity_bytes));
        }
        if (result == 0) {
            return platform_error_set(
                error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
                FW_ERROR_OPERATION_READ, uart_instance(uart),
                size_to_detail(capacity_bytes));
        }
        *bytes_read = 1U;

        status = platform_error_from_esp_err(
            uart_get_buffered_data_len(uart->port, &buffered_bytes), error,
            FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_READ,
            uart_instance(uart), size_to_detail(capacity_bytes));
        if (status != FW_STATUS_OK) {
            return status;
        }
    }

    const size_t remaining_capacity = capacity_bytes - *bytes_read;
    if ((remaining_capacity > 0U) && (buffered_bytes > 0U)) {
        const size_t drain_bytes =
            (buffered_bytes < remaining_capacity) ?
            buffered_bytes : remaining_capacity;
        const int result = uart_read_bytes(
            uart->port,
            data + *bytes_read,
            (uint32_t)drain_bytes,
            0U);
        if (result < 0) {
            return platform_error_set(
                error, FW_STATUS_IO, FW_ERROR_RESOURCE_UART,
                FW_ERROR_OPERATION_READ, uart_instance(uart),
                size_to_detail(capacity_bytes));
        }
        *bytes_read += (size_t)result;
    }

    if (*bytes_read == 0U) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_READ, uart_instance(uart),
            size_to_detail(capacity_bytes));
    }

    if (esp_timer_get_time() > deadline_us) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_READ, uart_instance(uart),
            size_to_detail(capacity_bytes));
    }
    return FW_STATUS_OK;
}

fw_status_t platform_uart_write_some(platform_uart_t *uart,
                                     const uint8_t *data,
                                     size_t length_bytes,
                                     size_t *bytes_written,
                                     uint32_t timeout_us,
                                     fw_error_context_t *error)
{
    platform_error_clear(error);

    if (bytes_written == NULL) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }
    *bytes_written = 0U;

    if (!uart_is_ready(uart)) {
        return platform_error_set(
            error,
            (uart == NULL) ? FW_STATUS_INVALID_ARGUMENT :
            FW_STATUS_NOT_INITIALIZED,
            FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_WRITE,
            uart_instance(uart), size_to_detail(length_bytes));
    }
    if ((data == NULL) || (length_bytes == 0U) ||
        (length_bytes > (size_t)UINT32_MAX) || (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    int result = uart_tx_chars(uart->port, (const char *)data,
                               (uint32_t)length_bytes);
    if (result < 0) {
        return platform_error_set(
            error, FW_STATUS_IO, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }
    if (result > 0) {
        *bytes_written = (size_t)result;
        if (esp_timer_get_time() > deadline_us) {
            return platform_error_set(
                error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
                FW_ERROR_OPERATION_WRITE, uart_instance(uart),
                size_to_detail(length_bytes));
        }
        return FW_STATUS_OK;
    }

    const TickType_t wait_ticks = ticks_until(deadline_us);
    if (wait_ticks == 0U) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }

    fw_status_t status = platform_error_from_esp_err(
        uart_wait_tx_done(uart->port, wait_ticks), error,
        FW_ERROR_RESOURCE_UART, FW_ERROR_OPERATION_WRITE,
        uart_instance(uart), size_to_detail(length_bytes));
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (esp_timer_get_time() > deadline_us) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }

    result = uart_tx_chars(uart->port, (const char *)data,
                           (uint32_t)length_bytes);
    if (result < 0) {
        return platform_error_set(
            error, FW_STATUS_IO, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }
    if (result == 0) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }

    *bytes_written = (size_t)result;
    if (esp_timer_get_time() > deadline_us) {
        return platform_error_set(
            error, FW_STATUS_TIMEOUT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_WRITE, uart_instance(uart),
            size_to_detail(length_bytes));
    }
    return FW_STATUS_OK;
}

fw_status_t platform_uart_get_baud_rate(const platform_uart_t *uart,
                                        uint32_t *requested_baud_rate,
                                        uint32_t *actual_baud_rate,
                                        fw_error_context_t *error)
{
    platform_error_clear(error);

    if ((uart == NULL) || (requested_baud_rate == NULL) ||
        (actual_baud_rate == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_READ, uart_instance(uart), 0U);
    }
    if (!uart->initialized) {
        return platform_error_set(
            error, FW_STATUS_NOT_INITIALIZED, FW_ERROR_RESOURCE_UART,
            FW_ERROR_OPERATION_READ, uart_instance(uart), 0U);
    }

    *requested_baud_rate = uart->requested_baud_rate;
    *actual_baud_rate = uart->actual_baud_rate;
    return FW_STATUS_OK;
}
