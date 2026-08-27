#include "platform_error.h"

void platform_error_clear(fw_error_context_t *error)
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

fw_status_t platform_error_set(fw_error_context_t *error,
                               fw_status_t status,
                               fw_error_resource_t resource,
                               fw_error_operation_t operation,
                               uint32_t instance,
                               uint32_t detail)
{
    if (status == FW_STATUS_OK) {
        platform_error_clear(error);
        return status;
    }

    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = resource,
            .operation = operation,
            .instance = instance,
            .detail = detail,
        };
    }

    return status;
}

fw_status_t platform_error_from_esp_err(
    esp_err_t result,
    fw_error_context_t *error,
    fw_error_resource_t resource,
    fw_error_operation_t operation,
    uint32_t instance,
    uint32_t detail)
{
    fw_status_t status;

    switch (result) {
    case ESP_OK:
        status = FW_STATUS_OK;
        break;
    case ESP_ERR_INVALID_ARG:
        status = FW_STATUS_INVALID_ARGUMENT;
        break;
    case ESP_ERR_INVALID_STATE:
        status = FW_STATUS_INVALID_STATE;
        break;
    case ESP_ERR_NOT_FOUND:
        status = FW_STATUS_NOT_FOUND;
        break;
    case ESP_ERR_TIMEOUT:
        status = FW_STATUS_TIMEOUT;
        break;
    case ESP_ERR_NOT_SUPPORTED:
        status = FW_STATUS_UNSUPPORTED;
        break;
    case ESP_ERR_NO_MEM:
        status = FW_STATUS_INTERNAL;
        break;
    default:
        status = FW_STATUS_IO;
        break;
    }

    return platform_error_set(
        error, status, resource, operation, instance, detail);
}
