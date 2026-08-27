#include "platform_spi.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
    platform_gpio_pin_t chip_select_pin;
    platform_gpio_level_t chip_select_active_level;
    platform_gpio_level_t chip_select_inactive_level;
    uint8_t filler_byte;
    uint32_t requested_clock_hz;
    uint32_t actual_clock_hz;
    uint32_t maximum_clock_hz;
    uint32_t chip_select_setup_us;
    uint32_t chip_select_hold_us;
};

static fw_status_t status_from_esp_err(esp_err_t result)
{
    switch (result) {
    case ESP_OK:
        return FW_STATUS_OK;
    case ESP_ERR_INVALID_ARG:
        return FW_STATUS_INVALID_ARGUMENT;
    case ESP_ERR_INVALID_STATE:
        return FW_STATUS_INVALID_STATE;
    case ESP_ERR_NOT_FOUND:
        return FW_STATUS_NOT_FOUND;
    case ESP_ERR_TIMEOUT:
        return FW_STATUS_TIMEOUT;
    case ESP_ERR_NOT_SUPPORTED:
        return FW_STATUS_UNSUPPORTED;
    case ESP_ERR_NO_MEM:
        return FW_STATUS_INTERNAL;
    default:
        return FW_STATUS_IO;
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

static bool cs_polarity_to_levels(
    platform_spi_cs_polarity_t polarity,
    platform_gpio_level_t *active_level,
    platform_gpio_level_t *inactive_level)
{
    switch (polarity) {
    case PLATFORM_SPI_CS_ACTIVE_LOW:
        *active_level = PLATFORM_GPIO_LEVEL_LOW;
        *inactive_level = PLATFORM_GPIO_LEVEL_HIGH;
        return true;
    case PLATFORM_SPI_CS_ACTIVE_HIGH:
        *active_level = PLATFORM_GPIO_LEVEL_HIGH;
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
        return status_from_esp_err(result);
    }
    if (completed_transaction != &bus->pending_device->transaction) {
        return FW_STATUS_INTERNAL;
    }

    bus->pending_device = NULL;
    return (esp_timer_get_time() <= deadline_us) ?
           FW_STATUS_OK : FW_STATUS_TIMEOUT;
}

static fw_status_t set_chip_select(platform_spi_device_t *device,
                                   platform_gpio_level_t level)
{
    return platform_gpio_write(device->chip_select_pin, level);
}

static fw_status_t execute_transaction_locked(
    platform_spi_device_t *device,
    size_t length_bytes,
    uint32_t override_clock_hz,
    bool use_chip_select,
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

    bool chip_select_asserted = false;
    if (use_chip_select) {
        status = set_chip_select(device, device->chip_select_active_level);
        if (status != FW_STATUS_OK) {
            return status;
        }
        chip_select_asserted = true;
        if (device->chip_select_setup_us > 0U) {
            esp_rom_delay_us(device->chip_select_setup_us);
        }
    }

    const TickType_t queue_wait_ticks = ticks_until(deadline_us);
    if (queue_wait_ticks == 0U) {
        status = FW_STATUS_TIMEOUT;
        goto cleanup;
    }

    status = status_from_esp_err(spi_device_queue_trans(
        device->handle,
        &device->transaction,
        queue_wait_ticks));
    if (status != FW_STATUS_OK) {
        goto cleanup;
    }
    bus->pending_device = device;

    status = drain_pending_transaction_locked(bus, deadline_us);

cleanup:
    if (chip_select_asserted) {
        if (device->chip_select_hold_us > 0U) {
            esp_rom_delay_us(device->chip_select_hold_us);
        }
        const fw_status_t cs_status = set_chip_select(
            device, device->chip_select_inactive_level);
        if ((status == FW_STATUS_OK) && (cs_status != FW_STATUS_OK)) {
            status = cs_status;
        }
    }

    return status;
}

fw_status_t platform_spi_bus_initialize(
    const platform_spi_bus_config_t *config,
    platform_spi_bus_t **bus)
{
    spi_host_device_t idf_host;

    if ((config == NULL) || (bus == NULL)) {
        return FW_STATUS_INVALID_ARGUMENT;
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
        return FW_STATUS_INVALID_ARGUMENT;
    }

    platform_spi_bus_t *new_bus = heap_caps_calloc(
        1U, sizeof(*new_bus), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (new_bus == NULL) {
        return FW_STATUS_INTERNAL;
    }

    new_bus->host = idf_host;
    new_bus->dma_enabled = config->dma_enabled;
    new_bus->maximum_transfer_size_bytes =
        config->maximum_transfer_size_bytes;
    new_bus->mutex = xSemaphoreCreateMutexStatic(&new_bus->mutex_storage);
    if (new_bus->mutex == NULL) {
        heap_caps_free(new_bus);
        return FW_STATUS_INTERNAL;
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
    const fw_status_t status = status_from_esp_err(spi_bus_initialize(
        idf_host, &idf_config, dma_channel));
    if (status != FW_STATUS_OK) {
        vSemaphoreDelete(new_bus->mutex);
        heap_caps_free(new_bus);
        return status;
    }

    *bus = new_bus;
    return FW_STATUS_OK;
}

fw_status_t platform_spi_bus_deinitialize(platform_spi_bus_t *bus,
                                          uint32_t timeout_us)
{
    if ((bus == NULL) || (timeout_us == 0U)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = drain_pending_transaction_locked(bus, deadline_us);
    if ((status == FW_STATUS_OK) && (bus->device_count != 0U)) {
        status = FW_STATUS_INVALID_STATE;
    }
    if (status == FW_STATUS_OK) {
        status = status_from_esp_err(spi_bus_free(bus->host));
    }

    (void)xSemaphoreGive(bus->mutex);
    if (status == FW_STATUS_OK) {
        vSemaphoreDelete(bus->mutex);
        heap_caps_free(bus);
    }

    return status;
}

fw_status_t platform_spi_device_add(
    platform_spi_bus_t *bus,
    const platform_spi_device_config_t *config,
    platform_spi_device_t **device)
{
    platform_gpio_level_t active_level;
    platform_gpio_level_t inactive_level;

    if ((bus == NULL) || (config == NULL) || (device == NULL)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    *device = NULL;

    if (!is_valid_output_pin(config->chip_select_pin) ||
        !cs_polarity_to_levels(config->chip_select_polarity,
                               &active_level,
                               &inactive_level) ||
        !bit_order_is_valid(config->bit_order) || (config->mode > 3U) ||
        (config->initial_clock_hz == 0U) ||
        (config->initial_clock_hz > config->maximum_clock_hz) ||
        (config->maximum_clock_hz > (uint32_t)INT_MAX) ||
        (config->input_delay_ns > (uint32_t)INT_MAX) ||
        (config->maximum_transfer_size_bytes == 0U) ||
        (config->maximum_transfer_size_bytes >
         bus->maximum_transfer_size_bytes)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    platform_spi_device_t *new_device = heap_caps_calloc(
        1U, sizeof(*new_device), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (new_device == NULL) {
        return FW_STATUS_INTERNAL;
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
        return FW_STATUS_INTERNAL;
    }

    new_device->bus = bus;
    new_device->maximum_transfer_size_bytes =
        config->maximum_transfer_size_bytes;
    new_device->chip_select_pin = config->chip_select_pin;
    new_device->chip_select_active_level = active_level;
    new_device->chip_select_inactive_level = inactive_level;
    new_device->filler_byte = config->filler_byte;
    new_device->requested_clock_hz = config->initial_clock_hz;
    new_device->maximum_clock_hz = config->maximum_clock_hz;
    new_device->chip_select_setup_us = config->chip_select_setup_us;
    new_device->chip_select_hold_us = config->chip_select_hold_us;

    fw_status_t status = platform_gpio_configure_output(
        config->chip_select_pin, inactive_level);
    if (status != FW_STATUS_OK) {
        goto failure;
    }

    const spi_device_interface_config_t idf_config = {
        .mode = config->mode,
        .clock_speed_hz = (int)config->initial_clock_hz,
        .input_delay_ns = (int)config->input_delay_ns,
        .spics_io_num = -1,
        .flags = (config->bit_order == PLATFORM_SPI_BIT_ORDER_LSB_FIRST) ?
                 SPI_DEVICE_BIT_LSBFIRST : 0U,
        .queue_size = 1,
    };

    status = status_from_esp_err(spi_bus_add_device(
        bus->host, &idf_config, &new_device->handle));
    if (status != FW_STATUS_OK) {
        goto failure;
    }

    int actual_frequency_khz = 0;
    status = status_from_esp_err(spi_device_get_actual_freq(
        new_device->handle, &actual_frequency_khz));
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
    heap_caps_free(new_device->tx_buffer);
    heap_caps_free(new_device->rx_buffer);
    heap_caps_free(new_device);
    return status;
}

fw_status_t platform_spi_device_remove(platform_spi_device_t *device,
                                       uint32_t timeout_us)
{
    if ((device == NULL) || (timeout_us == 0U)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    platform_spi_bus_t *bus = device->bus;
    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = drain_pending_transaction_locked(bus, deadline_us);
    if (status == FW_STATUS_OK) {
        status = set_chip_select(device, device->chip_select_inactive_level);
    }
    if (status == FW_STATUS_OK) {
        status = status_from_esp_err(spi_bus_remove_device(device->handle));
    }
    if (status == FW_STATUS_OK) {
        bus->device_count--;
    }

    (void)xSemaphoreGive(bus->mutex);
    if (status == FW_STATUS_OK) {
        heap_caps_free(device->tx_buffer);
        heap_caps_free(device->rx_buffer);
        heap_caps_free(device);
    }

    return status;
}

fw_status_t platform_spi_transfer(void *context,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t length_bytes,
                                  uint32_t timeout_us)
{
    platform_spi_device_t *device = context;
    if ((device == NULL) || (length_bytes == 0U) ||
        (length_bytes > device->maximum_transfer_size_bytes) ||
        (length_bytes > (SIZE_MAX / 8U)) || (timeout_us == 0U)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(device->bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = drain_pending_transaction_locked(device->bus, deadline_us);
    if (status == FW_STATUS_OK) {
        if (tx_data != NULL) {
            memcpy(device->tx_buffer, tx_data, length_bytes);
        } else {
            memset(device->tx_buffer, device->filler_byte, length_bytes);
        }

        status = execute_transaction_locked(
            device, length_bytes, 0U, true, deadline_us);
    }
    if ((status == FW_STATUS_OK) && (rx_data != NULL)) {
        memcpy(rx_data, device->rx_buffer, length_bytes);
    }

    (void)xSemaphoreGive(device->bus->mutex);
    return status;
}

fw_status_t platform_spi_device_set_clock(platform_spi_device_t *device,
                                          uint32_t requested_clock_hz,
                                          uint32_t timeout_us,
                                          uint32_t *actual_clock_hz)
{
    if ((device == NULL) || (requested_clock_hz == 0U) ||
        (requested_clock_hz > device->maximum_clock_hz) ||
        (requested_clock_hz > (uint32_t)INT_MAX) ||
        (timeout_us == 0U) || (actual_clock_hz == NULL)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    const int64_t deadline_us = deadline_from_timeout(timeout_us);
    fw_status_t status = take_bus_mutex(device->bus, deadline_us);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = execute_transaction_locked(
        device, 0U, requested_clock_hz, false, deadline_us);
    if (status == FW_STATUS_OK) {
        int actual_frequency_khz = 0;
        status = status_from_esp_err(spi_device_get_actual_freq(
            device->handle, &actual_frequency_khz));
        if (status == FW_STATUS_OK) {
            device->requested_clock_hz = requested_clock_hz;
            device->actual_clock_hz =
                (uint32_t)actual_frequency_khz * UINT32_C(1000);
            *actual_clock_hz = device->actual_clock_hz;
        }
    }

    (void)xSemaphoreGive(device->bus->mutex);
    return status;
}

fw_status_t platform_spi_device_get_clock(
    const platform_spi_device_t *device,
    uint32_t *requested_clock_hz,
    uint32_t *actual_clock_hz)
{
    if ((device == NULL) || (requested_clock_hz == NULL) ||
        (actual_clock_hz == NULL)) {
        return FW_STATUS_INVALID_ARGUMENT;
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
        .context = device,
    };
    return interface;
}
