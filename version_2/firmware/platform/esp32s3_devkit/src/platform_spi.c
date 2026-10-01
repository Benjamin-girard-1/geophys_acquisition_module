#include "platform_spi.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform_error.h"

#define PLATFORM_SPI_BURST_BUFFER_COUNT UINT8_C(2)
#define PLATFORM_SPI_MAX_CS_SETUP_CYCLES UINT8_C(1)
#define PLATFORM_SPI_MAX_CS_HOLD_CYCLES UINT8_C(16)

struct platform_spi_bus {
    spi_host_device_t host;
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;
    struct platform_spi_device *pending_device;
    size_t maximum_transfer_size_bytes;
    size_t device_count;
    bool dma_enabled;
};

struct platform_spi_device {
    platform_spi_bus_t *bus;
    spi_device_handle_t handle;
    spi_transaction_t transaction;
    uint8_t *tx_buffer;
    uint8_t *rx_buffer;
    size_t maximum_transfer_size_bytes;
    uint8_t filler_byte;
    uint32_t requested_clock_hz;
    uint32_t actual_clock_hz;
    uint32_t maximum_clock_hz;
    spi_transaction_t burst_transactions[PLATFORM_SPI_BURST_BUFFER_COUNT];
    uint8_t *burst_rx_buffers[PLATFORM_SPI_BURST_BUFFER_COUNT];
    TaskHandle_t burst_owner;
    size_t burst_length_bytes;
    uint8_t burst_active_index;
    bool burst_transaction_active;
    bool burst_bus_acquired;
    bool burst_active;
};

static uint32_t host_instance_from_idf(spi_host_device_t host)
{
    switch (host) {
    case SPI2_HOST:
        return 2U;
    case SPI3_HOST:
        return 3U;
    default:
        return FW_ERROR_INSTANCE_NONE;
    }
}

static uint32_t host_instance_from_platform(platform_spi_host_t host)
{
    switch (host) {
    case PLATFORM_SPI_HOST_2:
        return 2U;
    case PLATFORM_SPI_HOST_3:
        return 3U;
    default:
        return FW_ERROR_INSTANCE_NONE;
    }
}

static uint32_t bus_instance(const platform_spi_bus_t *bus)
{
    return (bus == NULL) ? FW_ERROR_INSTANCE_NONE :
           host_instance_from_idf(bus->host);
}

static uint32_t size_to_detail(size_t size)
{
    return (size > (size_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)size;
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

static bool is_optional_input_pin_valid(platform_gpio_pin_t pin)
{
    return (pin == PLATFORM_SPI_PIN_UNUSED) || is_valid_input_pin(pin);
}

static bool is_optional_output_pin_valid(platform_gpio_pin_t pin)
{
    return (pin == PLATFORM_SPI_PIN_UNUSED) || is_valid_output_pin(pin);
}

static bool host_to_idf(platform_spi_host_t host,
                        spi_host_device_t *idf_host)
{
    switch (host) {
    case PLATFORM_SPI_HOST_2:
        *idf_host = SPI2_HOST;
        return true;
    case PLATFORM_SPI_HOST_3:
        *idf_host = SPI3_HOST;
        return true;
    default:
        return false;
    }
}

static bool bit_order_is_valid(platform_spi_bit_order_t bit_order)
{
    return (bit_order == PLATFORM_SPI_BIT_ORDER_MSB_FIRST) ||
           (bit_order == PLATFORM_SPI_BIT_ORDER_LSB_FIRST);
}

static bool cs_polarity_to_inactive_level(
    platform_spi_cs_polarity_t polarity,
    platform_gpio_level_t *inactive_level)
{
    switch (polarity) {
    case PLATFORM_SPI_CS_ACTIVE_LOW:
        *inactive_level = PLATFORM_GPIO_LEVEL_HIGH;
        return true;
    case PLATFORM_SPI_CS_ACTIVE_HIGH:
        *inactive_level = PLATFORM_GPIO_LEVEL_LOW;
        return true;
    default:
        return false;
    }
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

static fw_status_t take_bus_mutex(platform_spi_bus_t *bus,
                                  int64_t deadline_us)
{
    const TickType_t wait_ticks = ticks_until(deadline_us);
    if (wait_ticks == 0U) {
        return FW_STATUS_TIMEOUT;
    }
    if (xSemaphoreTake(bus->mutex, wait_ticks) != pdTRUE) {
        return FW_STATUS_TIMEOUT;
    }
    if (esp_timer_get_time() > deadline_us) {
        (void)xSemaphoreGive(bus->mutex);
        return FW_STATUS_TIMEOUT;
    }

    return FW_STATUS_OK;
}

static fw_status_t drain_pending_transaction_locked(
    platform_spi_bus_t *bus,
    int64_t deadline_us)
{
    if (bus->pending_device == NULL) {
        return FW_STATUS_OK;
    }

    spi_transaction_t *completed_transaction = NULL;
    const TickType_t wait_ticks = ticks_until(deadline_us);
    if (wait_ticks == 0U) {
        return FW_STATUS_TIMEOUT;
    }

    const esp_err_t result = spi_device_get_trans_result(
        bus->pending_device->handle,
        &completed_transaction,
        wait_ticks);
    if (result != ESP_OK) {
        return platform_error_from_esp_err(
            result, NULL, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER, bus_instance(bus), 0U);
    }
    if (completed_transaction != &bus->pending_device->transaction) {
        return FW_STATUS_INTERNAL;
    }

    bus->pending_device = NULL;
    return (esp_timer_get_time() <= deadline_us) ?
           FW_STATUS_OK : FW_STATUS_TIMEOUT;
}

static fw_status_t execute_transaction_locked(
    platform_spi_device_t *device,
    size_t length_bytes,
    uint32_t override_clock_hz,
    int64_t deadline_us)
{
    platform_spi_bus_t *bus = device->bus;
    fw_status_t status = drain_pending_transaction_locked(bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return status;
    }

    memset(&device->transaction, 0, sizeof(device->transaction));
    device->transaction.length = length_bytes * 8U;
    device->transaction.rxlength = length_bytes * 8U;
    device->transaction.override_freq_hz = override_clock_hz;
    if (length_bytes > 0U) {
        device->transaction.tx_buffer = device->tx_buffer;
        device->transaction.rx_buffer = device->rx_buffer;
    }

    const TickType_t queue_wait_ticks = ticks_until(deadline_us);
    if (queue_wait_ticks == 0U) {
        return FW_STATUS_TIMEOUT;
    }

    status = platform_error_from_esp_err(
        spi_device_queue_trans(device->handle, &device->transaction,
                               queue_wait_ticks),
        NULL, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_TRANSFER,
        bus_instance(bus), size_to_detail(length_bytes));
    if (status != FW_STATUS_OK) {
        return status;
    }
    bus->pending_device = device;

    return drain_pending_transaction_locked(bus, deadline_us);
}

fw_status_t platform_spi_bus_initialize(
    const platform_spi_bus_config_t *config,
    platform_spi_bus_t **bus,
    fw_error_context_t *error)
{
    spi_host_device_t idf_host;

    platform_error_clear(error);

    if ((config == NULL) || (bus == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_INITIALIZE,
            (config == NULL) ? FW_ERROR_INSTANCE_NONE :
            host_instance_from_platform(config->host),
            (config == NULL) ? 0U :
            size_to_detail(config->maximum_transfer_size_bytes));
    }
    *bus = NULL;

    if (!host_to_idf(config->host, &idf_host) ||
        !is_valid_output_pin(config->clock_pin) ||
        !is_optional_output_pin_valid(config->mosi_pin) ||
        !is_optional_input_pin_valid(config->miso_pin) ||
        ((config->mosi_pin == PLATFORM_SPI_PIN_UNUSED) &&
         (config->miso_pin == PLATFORM_SPI_PIN_UNUSED)) ||
        (config->maximum_transfer_size_bytes == 0U) ||
        (config->maximum_transfer_size_bytes > (size_t)INT_MAX)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_INITIALIZE,
            host_instance_from_platform(config->host),
            size_to_detail(config->maximum_transfer_size_bytes));
    }

    platform_spi_bus_t *new_bus = heap_caps_calloc(
        1U, sizeof(*new_bus), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (new_bus == NULL) {
        return platform_error_set(
            error, FW_STATUS_INTERNAL, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_INITIALIZE,
            host_instance_from_platform(config->host),
            size_to_detail(config->maximum_transfer_size_bytes));
    }

    new_bus->host = idf_host;
    new_bus->dma_enabled = config->dma_enabled;
    new_bus->maximum_transfer_size_bytes =
        config->maximum_transfer_size_bytes;
    new_bus->mutex = xSemaphoreCreateMutexStatic(&new_bus->mutex_storage);
    if (new_bus->mutex == NULL) {
        heap_caps_free(new_bus);
        return platform_error_set(
            error, FW_STATUS_INTERNAL, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_INITIALIZE,
            host_instance_from_platform(config->host),
            size_to_detail(config->maximum_transfer_size_bytes));
    }

    const spi_bus_config_t idf_config = {
        .mosi_io_num = (config->mosi_pin == PLATFORM_SPI_PIN_UNUSED) ?
                       -1 : (int)config->mosi_pin,
        .miso_io_num = (config->miso_pin == PLATFORM_SPI_PIN_UNUSED) ?
                       -1 : (int)config->miso_pin,
        .sclk_io_num = (int)config->clock_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)config->maximum_transfer_size_bytes,
    };

    const spi_dma_chan_t dma_channel = config->dma_enabled ?
        SPI_DMA_CH_AUTO : SPI_DMA_DISABLED;
    const fw_status_t status = platform_error_from_esp_err(
        spi_bus_initialize(idf_host, &idf_config, dma_channel), error,
        FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_INITIALIZE,
        host_instance_from_platform(config->host),
        size_to_detail(config->maximum_transfer_size_bytes));
    if (status != FW_STATUS_OK) {
        vSemaphoreDelete(new_bus->mutex);
        heap_caps_free(new_bus);
        return status;
    }

    *bus = new_bus;
    return FW_STATUS_OK;
}

fw_status_t platform_spi_bus_deinitialize(platform_spi_bus_t *bus,
                                          uint32_t timeout_us,
                                          fw_error_context_t *error)
{
    platform_error_clear(error);

    if ((bus == NULL) || (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DEINITIALIZE, bus_instance(bus), timeout_us);
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DEINITIALIZE, bus_instance(bus), timeout_us);
    }

    status = drain_pending_transaction_locked(bus, deadline_us);
    if ((status == FW_STATUS_OK) && (bus->device_count != 0U)) {
        status = FW_STATUS_INVALID_STATE;
    }
    if (status == FW_STATUS_OK) {
        status = platform_error_from_esp_err(
            spi_bus_free(bus->host), error, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DEINITIALIZE, bus_instance(bus), timeout_us);
    }

    (void)xSemaphoreGive(bus->mutex);
    if (status == FW_STATUS_OK) {
        vSemaphoreDelete(bus->mutex);
        heap_caps_free(bus);
    }

    return platform_error_set(
        error, status, FW_ERROR_RESOURCE_SPI,
        FW_ERROR_OPERATION_DEINITIALIZE,
        (status == FW_STATUS_OK) ? FW_ERROR_INSTANCE_NONE : bus_instance(bus),
        timeout_us);
}

fw_status_t platform_spi_device_add(
    platform_spi_bus_t *bus,
    const platform_spi_device_config_t *config,
    platform_spi_device_t **device,
    fw_error_context_t *error)
{
    platform_gpio_level_t inactive_level;

    platform_error_clear(error);

    if ((bus == NULL) || (config == NULL) || (device == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_ATTACH, bus_instance(bus),
            (config == NULL) ? 0U : config->chip_select_pin);
    }
    *device = NULL;

    if (!is_valid_output_pin(config->chip_select_pin) ||
        !cs_polarity_to_inactive_level(config->chip_select_polarity,
                                       &inactive_level) ||
        !bit_order_is_valid(config->bit_order) || (config->mode > 3U) ||
        (config->initial_clock_hz == 0U) ||
        (config->initial_clock_hz > config->maximum_clock_hz) ||
        (config->maximum_clock_hz > (uint32_t)INT_MAX) ||
        (config->input_delay_ns > (uint32_t)INT_MAX) ||
        (config->chip_select_setup_cycles >
         PLATFORM_SPI_MAX_CS_SETUP_CYCLES) ||
        (config->chip_select_hold_cycles >
         PLATFORM_SPI_MAX_CS_HOLD_CYCLES) ||
        (config->maximum_transfer_size_bytes == 0U) ||
        (config->maximum_transfer_size_bytes >
         bus->maximum_transfer_size_bytes)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_ATTACH, bus_instance(bus),
            config->chip_select_pin);
    }

    platform_spi_device_t *new_device = heap_caps_calloc(
        1U, sizeof(*new_device), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (new_device == NULL) {
        return platform_error_set(
            error, FW_STATUS_INTERNAL, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_ATTACH, bus_instance(bus),
            config->chip_select_pin);
    }

    const uint32_t buffer_capabilities = MALLOC_CAP_INTERNAL |
        MALLOC_CAP_8BIT | (bus->dma_enabled ? MALLOC_CAP_DMA : 0U);
    const size_t allocation_size =
        (config->maximum_transfer_size_bytes + 3U) & ~(size_t)3U;
    new_device->tx_buffer = heap_caps_malloc(
        allocation_size, buffer_capabilities);
    new_device->rx_buffer = heap_caps_malloc(
        allocation_size, buffer_capabilities);
    if ((new_device->tx_buffer == NULL) || (new_device->rx_buffer == NULL)) {
        heap_caps_free(new_device->tx_buffer);
        heap_caps_free(new_device->rx_buffer);
        heap_caps_free(new_device);
        return platform_error_set(
            error, FW_STATUS_INTERNAL, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_ATTACH, bus_instance(bus),
            config->chip_select_pin);
    }
    new_device->burst_rx_buffers[0] = new_device->rx_buffer;
    if (bus->dma_enabled) {
        for (uint8_t index = 1U;
             index < PLATFORM_SPI_BURST_BUFFER_COUNT;
             index++) {
            new_device->burst_rx_buffers[index] = heap_caps_malloc(
                allocation_size, buffer_capabilities);
            if (new_device->burst_rx_buffers[index] == NULL) {
                for (uint8_t release_index = 1U;
                     release_index < index;
                     release_index++) {
                    heap_caps_free(
                        new_device->burst_rx_buffers[release_index]);
                }
                heap_caps_free(new_device->tx_buffer);
                heap_caps_free(new_device->rx_buffer);
                heap_caps_free(new_device);
                return platform_error_set(
                    error, FW_STATUS_INTERNAL, FW_ERROR_RESOURCE_SPI,
                    FW_ERROR_OPERATION_ATTACH, bus_instance(bus),
                    config->chip_select_pin);
            }
        }
    }

    new_device->bus = bus;
    new_device->maximum_transfer_size_bytes =
        config->maximum_transfer_size_bytes;
    new_device->filler_byte = config->filler_byte;
    new_device->requested_clock_hz = config->initial_clock_hz;
    new_device->maximum_clock_hz = config->maximum_clock_hz;

    fw_status_t status = platform_gpio_configure_output(
        config->chip_select_pin, inactive_level, NULL);
    if (status != FW_STATUS_OK) {
        goto failure;
    }

    const spi_device_interface_config_t idf_config = {
        .mode = config->mode,
        .clock_speed_hz = (int)config->initial_clock_hz,
        .input_delay_ns = (int)config->input_delay_ns,
        .spics_io_num = (int)config->chip_select_pin,
        .cs_ena_pretrans = config->chip_select_setup_cycles,
        .cs_ena_posttrans = config->chip_select_hold_cycles,
        .flags =
            ((config->bit_order == PLATFORM_SPI_BIT_ORDER_LSB_FIRST) ?
             SPI_DEVICE_BIT_LSBFIRST : 0U) |
            ((config->chip_select_polarity ==
              PLATFORM_SPI_CS_ACTIVE_HIGH) ?
             SPI_DEVICE_POSITIVE_CS : 0U),
        .queue_size = 1,
    };

    status = platform_error_from_esp_err(
        spi_bus_add_device(bus->host, &idf_config, &new_device->handle),
        error, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_ATTACH,
        bus_instance(bus), config->chip_select_pin);
    if (status != FW_STATUS_OK) {
        goto failure;
    }

    int actual_frequency_khz = 0;
    status = platform_error_from_esp_err(
        spi_device_get_actual_freq(new_device->handle,
                                   &actual_frequency_khz),
        error, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_ATTACH,
        bus_instance(bus), config->chip_select_pin);
    if (status != FW_STATUS_OK) {
        (void)spi_bus_remove_device(new_device->handle);
        goto failure;
    }
    new_device->actual_clock_hz =
        (uint32_t)actual_frequency_khz * UINT32_C(1000);

    bus->device_count++;
    *device = new_device;
    return FW_STATUS_OK;

failure:
    for (uint8_t index = 1U;
         index < PLATFORM_SPI_BURST_BUFFER_COUNT;
         index++) {
        heap_caps_free(new_device->burst_rx_buffers[index]);
    }
    heap_caps_free(new_device->tx_buffer);
    heap_caps_free(new_device->rx_buffer);
    heap_caps_free(new_device);
    return platform_error_set(
        error, status, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_ATTACH,
        bus_instance(bus), config->chip_select_pin);
}

fw_status_t platform_spi_device_remove(platform_spi_device_t *device,
                                       uint32_t timeout_us,
                                       fw_error_context_t *error)
{
    platform_error_clear(error);

    if ((device == NULL) || (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DETACH,
            (device == NULL) ? FW_ERROR_INSTANCE_NONE :
            bus_instance(device->bus),
            timeout_us);
    }
    if (device->burst_active) {
        return platform_error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DETACH, bus_instance(device->bus),
            timeout_us);
    }

    platform_spi_bus_t *bus = device->bus;
    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DETACH, bus_instance(bus), timeout_us);
    }

    status = drain_pending_transaction_locked(bus, deadline_us);
    if (status == FW_STATUS_OK) {
        status = platform_error_from_esp_err(
            spi_bus_remove_device(device->handle), error,
            FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_DETACH,
            bus_instance(bus), timeout_us);
    }
    if (status == FW_STATUS_OK) {
        bus->device_count--;
    }

    (void)xSemaphoreGive(bus->mutex);
    if (status == FW_STATUS_OK) {
        for (uint8_t index = 1U;
             index < PLATFORM_SPI_BURST_BUFFER_COUNT;
             index++) {
            heap_caps_free(device->burst_rx_buffers[index]);
        }
        heap_caps_free(device->tx_buffer);
        heap_caps_free(device->rx_buffer);
        heap_caps_free(device);
    }

    return platform_error_set(
        error, status, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_DETACH,
        (status == FW_STATUS_OK) ? FW_ERROR_INSTANCE_NONE : bus_instance(bus),
        timeout_us);
}

fw_status_t platform_spi_transfer(void *context,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t length_bytes,
                                  uint32_t timeout_us,
                                  fw_error_context_t *error)
{
    platform_spi_device_t *device = context;

    platform_error_clear(error);

    if ((device == NULL) || (length_bytes == 0U) ||
        (length_bytes > device->maximum_transfer_size_bytes) ||
        (length_bytes > (SIZE_MAX / 8U)) || (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER,
            (device == NULL) ? FW_ERROR_INSTANCE_NONE :
            bus_instance(device->bus),
            size_to_detail(length_bytes));
    }
    if (device->burst_active) {
        return platform_error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER, bus_instance(device->bus),
            size_to_detail(length_bytes));
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(device->bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER, bus_instance(device->bus),
            size_to_detail(length_bytes));
    }

    status = drain_pending_transaction_locked(device->bus, deadline_us);
    if (status == FW_STATUS_OK) {
        if (tx_data != NULL) {
            memcpy(device->tx_buffer, tx_data, length_bytes);
        } else {
            memset(device->tx_buffer, device->filler_byte, length_bytes);
        }

        status = execute_transaction_locked(
            device, length_bytes, 0U, deadline_us);
    }
    if ((status == FW_STATUS_OK) && (rx_data != NULL)) {
        memcpy(rx_data, device->rx_buffer, length_bytes);
    }

    (void)xSemaphoreGive(device->bus->mutex);
    return platform_error_set(
        error, status, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_TRANSFER,
        bus_instance(device->bus), size_to_detail(length_bytes));
}

static void fill_repeating_pattern(uint8_t *destination,
                                   size_t destination_length,
                                   const uint8_t *pattern,
                                   size_t pattern_length)
{
    size_t filled = pattern_length;
    if (filled > destination_length) {
        filled = destination_length;
    }
    memcpy(destination, pattern, filled);
    while (filled < destination_length) {
        size_t copy_length = filled;
        if (copy_length > destination_length - filled) {
            copy_length = destination_length - filled;
        }
        memcpy(destination + filled, destination, copy_length);
        filled += copy_length;
    }
}

static fw_status_t finish_burst_transaction(
    platform_spi_device_t *device,
    TickType_t wait_ticks,
    const uint8_t **completed_rx_data,
    size_t *completed_length_bytes,
    fw_error_operation_t operation,
    fw_error_context_t *error)
{
    if (!device->burst_transaction_active) {
        return FW_STATUS_OK;
    }

    fw_status_t status = platform_error_from_esp_err(
        spi_device_polling_end(device->handle, wait_ticks), error,
        FW_ERROR_RESOURCE_SPI, operation, bus_instance(device->bus),
        size_to_detail(device->burst_length_bytes));
    if (status != FW_STATUS_OK) {
        return status;
    }

    *completed_rx_data =
        device->burst_rx_buffers[device->burst_active_index];
    *completed_length_bytes = device->burst_length_bytes;
    device->burst_transaction_active = false;
    return FW_STATUS_OK;
}

fw_status_t platform_spi_burst_prepare(
    void *context,
    const uint8_t *pattern,
    size_t pattern_length_bytes,
    size_t burst_length_bytes,
    uint32_t timeout_us,
    fw_error_context_t *error)
{
    platform_spi_device_t *device = context;
    platform_error_clear(error);

    if ((device == NULL) || (pattern == NULL) ||
        (pattern_length_bytes == 0U) || (burst_length_bytes == 0U) ||
        (burst_length_bytes > (SIZE_MAX / 8U)) ||
        (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER,
            (device == NULL) ? FW_ERROR_INSTANCE_NONE :
            bus_instance(device->bus), size_to_detail(burst_length_bytes));
    }
    if (!device->bus->dma_enabled || device->burst_active ||
        burst_length_bytes > device->maximum_transfer_size_bytes) {
        return platform_error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER, bus_instance(device->bus),
            size_to_detail(burst_length_bytes));
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(device->bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER, bus_instance(device->bus),
            size_to_detail(burst_length_bytes));
    }

    status = drain_pending_transaction_locked(device->bus, deadline_us);
    if (status != FW_STATUS_OK) {
        (void)xSemaphoreGive(device->bus->mutex);
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER, bus_instance(device->bus),
            size_to_detail(burst_length_bytes));
    }

    fill_repeating_pattern(device->tx_buffer, burst_length_bytes,
                           pattern, pattern_length_bytes);
    device->burst_length_bytes = burst_length_bytes;
    device->burst_active_index = 0U;
    device->burst_transaction_active = false;
    device->burst_owner = xTaskGetCurrentTaskHandle();
    for (uint8_t index = 0U;
         index < PLATFORM_SPI_BURST_BUFFER_COUNT;
         index++) {
        spi_transaction_t *transaction =
            &device->burst_transactions[index];
        memset(transaction, 0, sizeof(*transaction));
        transaction->length = burst_length_bytes * 8U;
        transaction->rxlength = burst_length_bytes * 8U;
        transaction->tx_buffer = device->tx_buffer;
        transaction->rx_buffer = device->burst_rx_buffers[index];
    }

    /* ESP-IDF 5.5 accepts only portMAX_DELAY for bus acquisition. The
     * platform mutex above already applied the public finite timeout. */
    status = platform_error_from_esp_err(
        spi_device_acquire_bus(device->handle, portMAX_DELAY), NULL,
        FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_TRANSFER,
        bus_instance(device->bus), size_to_detail(burst_length_bytes));
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    device->burst_bus_acquired = true;

    device->burst_active = true;
    return FW_STATUS_OK;

failure:
    if (device->burst_bus_acquired) {
        spi_device_release_bus(device->handle);
        device->burst_bus_acquired = false;
    }
    device->burst_owner = NULL;
    device->burst_length_bytes = 0U;
    (void)xSemaphoreGive(device->bus->mutex);
    return platform_error_set(
        error, status, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_TRANSFER,
        bus_instance(device->bus), size_to_detail(burst_length_bytes));
}

fw_status_t platform_spi_burst_trigger(
    void *context,
    const uint8_t **completed_rx_data,
    size_t *completed_length_bytes,
    uint32_t timeout_us,
    fw_error_context_t *error)
{
    platform_spi_device_t *device = context;
    platform_error_clear(error);

    if ((device == NULL) || (completed_rx_data == NULL) ||
        (completed_length_bytes == NULL) || (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER,
            (device == NULL) ? FW_ERROR_INSTANCE_NONE :
            bus_instance(device->bus), timeout_us);
    }
    *completed_rx_data = NULL;
    *completed_length_bytes = 0U;
    if (!device->burst_active ||
        device->burst_owner != xTaskGetCurrentTaskHandle()) {
        return platform_error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_TRANSFER, bus_instance(device->bus),
            timeout_us);
    }

    uint8_t next_index = 0U;
    if (device->burst_transaction_active) {
        const uint8_t completed_index = device->burst_active_index;
        fw_status_t status = finish_burst_transaction(
            device, 0U, completed_rx_data, completed_length_bytes,
            FW_ERROR_OPERATION_TRANSFER, error);
        if (status != FW_STATUS_OK) {
            return status;
        }
        next_index = (uint8_t)(completed_index ^ UINT8_C(1));
    }

    fw_status_t status = platform_error_from_esp_err(
        spi_device_polling_start(
            device->handle, &device->burst_transactions[next_index],
            portMAX_DELAY),
        error, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_TRANSFER,
        bus_instance(device->bus),
        size_to_detail(device->burst_length_bytes));
    if (status != FW_STATUS_OK) {
        return status;
    }
    device->burst_active_index = next_index;
    device->burst_transaction_active = true;
    return FW_STATUS_OK;
}

fw_status_t platform_spi_burst_finish(
    void *context,
    const uint8_t **completed_rx_data,
    size_t *completed_length_bytes,
    uint32_t timeout_us,
    fw_error_context_t *error)
{
    platform_spi_device_t *device = context;
    platform_error_clear(error);

    if ((device == NULL) || (completed_rx_data == NULL) ||
        (completed_length_bytes == NULL) || (timeout_us == 0U)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DISABLE,
            (device == NULL) ? FW_ERROR_INSTANCE_NONE :
            bus_instance(device->bus), timeout_us);
    }
    *completed_rx_data = NULL;
    *completed_length_bytes = 0U;
    if (!device->burst_active ||
        device->burst_owner != xTaskGetCurrentTaskHandle()) {
        return platform_error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DISABLE, bus_instance(device->bus),
            timeout_us);
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    const TickType_t wait_ticks = ticks_until(deadline_us);
    fw_status_t status = finish_burst_transaction(
        device, wait_ticks, completed_rx_data, completed_length_bytes,
        FW_ERROR_OPERATION_DISABLE, error);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_DISABLE, bus_instance(device->bus),
            timeout_us);
    }

    if (device->burst_bus_acquired) {
        spi_device_release_bus(device->handle);
        device->burst_bus_acquired = false;
    }
    device->burst_active = false;
    device->burst_owner = NULL;
    device->burst_length_bytes = 0U;
    (void)xSemaphoreGive(device->bus->mutex);

    return platform_error_set(
        error, status, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_DISABLE,
        bus_instance(device->bus), timeout_us);
}

fw_status_t platform_spi_device_set_clock(platform_spi_device_t *device,
                                          uint32_t requested_clock_hz,
                                          uint32_t timeout_us,
                                          uint32_t *actual_clock_hz,
                                          fw_error_context_t *error)
{
    platform_error_clear(error);

    if ((device == NULL) || (requested_clock_hz == 0U) ||
        (requested_clock_hz > device->maximum_clock_hz) ||
        (requested_clock_hz > (uint32_t)INT_MAX) ||
        (timeout_us == 0U) || (actual_clock_hz == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_SET_CLOCK,
            (device == NULL) ? FW_ERROR_INSTANCE_NONE :
            bus_instance(device->bus),
            requested_clock_hz);
    }
    if (device->burst_active) {
        return platform_error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_SET_CLOCK, bus_instance(device->bus),
            requested_clock_hz);
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(device->bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_SET_CLOCK, bus_instance(device->bus),
            requested_clock_hz);
    }

    status = execute_transaction_locked(
        device, 0U, requested_clock_hz, deadline_us);
    if (status == FW_STATUS_OK) {
        int actual_frequency_khz = 0;
        status = platform_error_from_esp_err(
            spi_device_get_actual_freq(device->handle,
                                       &actual_frequency_khz),
            error, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_SET_CLOCK,
            bus_instance(device->bus), requested_clock_hz);
        if (status == FW_STATUS_OK) {
            device->requested_clock_hz = requested_clock_hz;
            device->actual_clock_hz =
                (uint32_t)actual_frequency_khz * UINT32_C(1000);
            *actual_clock_hz = device->actual_clock_hz;
        }
    }

    (void)xSemaphoreGive(device->bus->mutex);
    return platform_error_set(
        error, status, FW_ERROR_RESOURCE_SPI, FW_ERROR_OPERATION_SET_CLOCK,
        bus_instance(device->bus), requested_clock_hz);
}

fw_status_t platform_spi_device_get_clock(
    const platform_spi_device_t *device,
    uint32_t *requested_clock_hz,
    uint32_t *actual_clock_hz,
    fw_error_context_t *error)
{
    platform_error_clear(error);

    if ((device == NULL) || (requested_clock_hz == NULL) ||
        (actual_clock_hz == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_SPI,
            FW_ERROR_OPERATION_GET_CLOCK,
            (device == NULL) ? FW_ERROR_INSTANCE_NONE :
            bus_instance(device->bus),
            0U);
    }

    *requested_clock_hz = device->requested_clock_hz;
    *actual_clock_hz = device->actual_clock_hz;
    return FW_STATUS_OK;
}

fw_spi_interface_t platform_spi_device_interface(
    platform_spi_device_t *device)
{
    const fw_spi_interface_t interface = {
        .transfer = platform_spi_transfer,
        .burst_prepare = platform_spi_burst_prepare,
        .burst_trigger = platform_spi_burst_trigger,
        .burst_finish = platform_spi_burst_finish,
        .context = device,
    };
    return interface;
}
