#include "74hc_hct595.h"

#include <stddef.h>

static void error_clear(fw_error_context_t *error)
{
    if (error == NULL) {
        return;
    }

    *error = (fw_error_context_t) {
        .status = FW_STATUS_OK,
        .resource = FW_ERROR_RESOURCE_NONE,
        .operation = FW_ERROR_OPERATION_NONE,
        .instance = FW_ERROR_INSTANCE_NONE,
        .detail = 0U,
    };
}

static fw_status_t error_set(fw_error_context_t *error,
                             fw_status_t status,
                             fw_error_operation_t operation,
                             uint32_t instance,
                             uint32_t detail)
{
    if (status == FW_STATUS_OK) {
        error_clear(error);
        return status;
    }

    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_GPIO_EXPANDER,
            .operation = operation,
            .instance = instance,
            .detail = detail,
        };
    }

    return status;
}

static uint32_t device_instance(const hc595_t *device)
{
    return (device == NULL) ? FW_ERROR_INSTANCE_NONE :
           device->config.instance;
}

static fw_status_t normalize_callback_failure(
    const hc595_t *device,
    fw_status_t status,
    fw_error_context_t *error,
    fw_error_operation_t operation,
    uint32_t detail)
{
    if (status == FW_STATUS_OK) {
        return status;
    }

    if ((error != NULL) && (error->status == FW_STATUS_OK)) {
        return error_set(
            error, status, operation, device_instance(device), detail);
    }

    return status;
}

static fw_status_t write_line(hc595_t *device,
                              hc595_line_t line,
                              bool high,
                              fw_error_context_t *error,
                              fw_error_operation_t operation,
                              uint32_t detail)
{
    const fw_status_t status = device->config.write_line(
        device->config.context, line, high, error);
    return normalize_callback_failure(
        device, status, error, operation, detail);
}

static void edge_delay(const hc595_t *device)
{
    device->config.delay_us(
        device->config.context, device->config.edge_delay_us);
}

static fw_status_t prepare_serial_lines(hc595_t *device,
                                        fw_error_context_t *error,
                                        fw_error_operation_t operation,
                                        uint32_t detail)
{
    fw_status_t status = write_line(
        device, HC595_LINE_STORAGE_CLOCK, false, error, operation, detail);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = write_line(
        device, HC595_LINE_SHIFT_CLOCK, false, error, operation, detail);
    if (status != FW_STATUS_OK) {
        return status;
    }

    return write_line(
        device, HC595_LINE_SERIAL_DATA, false, error, operation, detail);
}

static fw_status_t shift_and_latch(hc595_t *device,
                                   uint16_t image,
                                   fw_error_context_t *error,
                                   fw_error_operation_t operation)
{
    const uint32_t detail = (uint32_t)image;
    fw_status_t status = prepare_serial_lines(
        device, error, operation, detail);
    if (status != FW_STATUS_OK) {
        return status;
    }

    for (uint8_t output = 0U; output < HC595_OUTPUT_COUNT; output++) {
        const bool high =
            ((image >> output) & UINT16_C(1)) != UINT16_C(0);

        status = write_line(
            device, HC595_LINE_SERIAL_DATA, high, error, operation, detail);
        if (status != FW_STATUS_OK) {
            return status;
        }
        edge_delay(device);

        status = write_line(
            device, HC595_LINE_SHIFT_CLOCK, true, error, operation, detail);
        if (status != FW_STATUS_OK) {
            return status;
        }
        edge_delay(device);

        status = write_line(
            device, HC595_LINE_SHIFT_CLOCK, false, error, operation, detail);
        if (status != FW_STATUS_OK) {
            return status;
        }
        edge_delay(device);
    }

    status = write_line(
        device, HC595_LINE_STORAGE_CLOCK, true, error, operation, detail);
    if (status != FW_STATUS_OK) {
        return status;
    }

    /* The parallel outputs changed at the successful rising latch edge. */
    device->shadow = image;
    edge_delay(device);

    status = write_line(
        device, HC595_LINE_STORAGE_CLOCK, false, error, operation, detail);
    if (status != FW_STATUS_OK) {
        return status;
    }
    edge_delay(device);

    return write_line(
        device, HC595_LINE_SERIAL_DATA, false, error, operation, detail);
}

static fw_status_t require_initialized(const hc595_t *device,
                                       fw_error_context_t *error,
                                       fw_error_operation_t operation,
                                       uint32_t detail)
{
    if (device == NULL) {
        return error_set(
            error, FW_STATUS_INVALID_ARGUMENT, operation,
            FW_ERROR_INSTANCE_NONE, detail);
    }
    if (!device->initialized) {
        return error_set(
            error, FW_STATUS_NOT_INITIALIZED, operation,
            device_instance(device), detail);
    }

    return FW_STATUS_OK;
}

fw_status_t hc595_initialize(hc595_t *device,
                             const hc595_config_t *config,
                             uint16_t initial_image,
                             fw_error_context_t *error)
{
    error_clear(error);

    if ((device == NULL) || (config == NULL) ||
        (config->write_line == NULL) || (config->delay_us == NULL) ||
        (config->edge_delay_us == 0U)) {
        return error_set(
            error, FW_STATUS_INVALID_ARGUMENT,
            FW_ERROR_OPERATION_INITIALIZE,
            (config == NULL) ? FW_ERROR_INSTANCE_NONE : config->instance,
            (uint32_t)initial_image);
    }
    if (device->initialized) {
        return error_set(
            error, FW_STATUS_INVALID_STATE, FW_ERROR_OPERATION_INITIALIZE,
            device_instance(device), (uint32_t)initial_image);
    }

    *device = (hc595_t) {
        .config = *config,
        .shadow = 0U,
        .outputs_enabled = false,
        .initialized = false,
    };

    fw_status_t status = write_line(
        device, HC595_LINE_OUTPUT_ENABLE_N, true, error,
        FW_ERROR_OPERATION_INITIALIZE, (uint32_t)initial_image);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = shift_and_latch(
        device, initial_image, error, FW_ERROR_OPERATION_INITIALIZE);
    if (status != FW_STATUS_OK) {
        return status;
    }

    device->initialized = true;
    return FW_STATUS_OK;
}

fw_status_t hc595_write_image(hc595_t *device,
                              uint16_t image,
                              fw_error_context_t *error)
{
    error_clear(error);

    const fw_status_t status = require_initialized(
        device, error, FW_ERROR_OPERATION_WRITE, (uint32_t)image);
    if (status != FW_STATUS_OK) {
        return status;
    }

    return shift_and_latch(
        device, image, error, FW_ERROR_OPERATION_WRITE);
}

fw_status_t hc595_update_masked(hc595_t *device,
                                uint16_t clear_mask,
                                uint16_t set_mask,
                                fw_error_context_t *error)
{
    error_clear(error);

    const uint32_t detail =
        ((uint32_t)clear_mask << 16U) | (uint32_t)set_mask;
    fw_status_t status = require_initialized(
        device, error, FW_ERROR_OPERATION_WRITE, detail);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if ((clear_mask & set_mask) != UINT16_C(0)) {
        return error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_OPERATION_WRITE,
            device_instance(device), detail);
    }

    const uint16_t image = (uint16_t)(
        (device->shadow & (uint16_t)~clear_mask) | set_mask);
    return shift_and_latch(
        device, image, error, FW_ERROR_OPERATION_WRITE);
}

fw_status_t hc595_set_output(hc595_t *device,
                             uint8_t output_index,
                             bool high,
                             fw_error_context_t *error)
{
    error_clear(error);

    fw_status_t status = require_initialized(
        device, error, FW_ERROR_OPERATION_WRITE, (uint32_t)output_index);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (output_index >= HC595_OUTPUT_COUNT) {
        return error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_OPERATION_WRITE,
            device_instance(device), (uint32_t)output_index);
    }

    const uint16_t mask = (uint16_t)(UINT16_C(1) << output_index);
    const uint16_t image = high ?
        (uint16_t)(device->shadow | mask) :
        (uint16_t)(device->shadow & (uint16_t)~mask);
    return shift_and_latch(
        device, image, error, FW_ERROR_OPERATION_WRITE);
}

fw_status_t hc595_get_shadow(const hc595_t *device,
                             uint16_t *image,
                             fw_error_context_t *error)
{
    error_clear(error);

    fw_status_t status = require_initialized(
        device, error, FW_ERROR_OPERATION_READ, 0U);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (image == NULL) {
        return error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_OPERATION_READ,
            device_instance(device), 0U);
    }

    *image = device->shadow;
    return FW_STATUS_OK;
}

fw_status_t hc595_set_outputs_enabled(hc595_t *device,
                                      bool enabled,
                                      fw_error_context_t *error)
{
    error_clear(error);

    const fw_error_operation_t operation = enabled ?
        FW_ERROR_OPERATION_ENABLE : FW_ERROR_OPERATION_DISABLE;
    fw_status_t status = require_initialized(
        device, error, operation, enabled ? 1U : 0U);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = write_line(
        device, HC595_LINE_OUTPUT_ENABLE_N, !enabled, error, operation,
        enabled ? 1U : 0U);
    if (status != FW_STATUS_OK) {
        return status;
    }

    device->outputs_enabled = enabled;
    edge_delay(device);
    return FW_STATUS_OK;
}

fw_status_t hc595_get_outputs_enabled(const hc595_t *device,
                                      bool *enabled,
                                      fw_error_context_t *error)
{
    error_clear(error);

    fw_status_t status = require_initialized(
        device, error, FW_ERROR_OPERATION_READ, 0U);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (enabled == NULL) {
        return error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_OPERATION_READ,
            device_instance(device), 0U);
    }

    *enabled = device->outputs_enabled;
    return FW_STATUS_OK;
}
