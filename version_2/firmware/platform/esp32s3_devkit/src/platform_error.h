#ifndef GEOPHYS_PLATFORM_ERROR_H
#define GEOPHYS_PLATFORM_ERROR_H

#include <stdint.h>

#include "esp_err.h"
#include "fw_error.h"

void platform_error_clear(fw_error_context_t *error);

fw_status_t platform_error_set(fw_error_context_t *error,
                               fw_status_t status,
                               fw_error_resource_t resource,
                               fw_error_operation_t operation,
                               uint32_t instance,
                               uint32_t detail);

fw_status_t platform_error_from_esp_err(
    esp_err_t result,
    fw_error_context_t *error,
    fw_error_resource_t resource,
    fw_error_operation_t operation,
    uint32_t instance,
    uint32_t detail);

#endif /* GEOPHYS_PLATFORM_ERROR_H */
