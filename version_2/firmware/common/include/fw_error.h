#ifndef GEOPHYS_FW_ERROR_H
#define GEOPHYS_FW_ERROR_H

#include <stdint.h>

#include "fw_status.h"

#define FW_ERROR_INSTANCE_NONE UINT32_MAX

/** @brief Portable category of the resource involved in a failed operation. */
typedef enum {
    FW_ERROR_RESOURCE_NONE = 0,
    FW_ERROR_RESOURCE_GPIO,
    FW_ERROR_RESOURCE_SPI,
    FW_ERROR_RESOURCE_UART,
    FW_ERROR_RESOURCE_GPIO_EXPANDER,
    FW_ERROR_RESOURCE_TIMER,
    FW_ERROR_RESOURCE_MEMORY,
    FW_ERROR_RESOURCE_SYNCHRONIZATION,
    FW_ERROR_RESOURCE_ADC,
} fw_error_resource_t;

/** @brief Portable operation that was in progress when a failure occurred. */
typedef enum {
    FW_ERROR_OPERATION_NONE = 0,
    FW_ERROR_OPERATION_INITIALIZE,
    FW_ERROR_OPERATION_DEINITIALIZE,
    FW_ERROR_OPERATION_CONFIGURE,
    FW_ERROR_OPERATION_READ,
    FW_ERROR_OPERATION_WRITE,
    FW_ERROR_OPERATION_TRANSFER,
    FW_ERROR_OPERATION_WAIT,
    FW_ERROR_OPERATION_ATTACH,
    FW_ERROR_OPERATION_DETACH,
    FW_ERROR_OPERATION_ENABLE,
    FW_ERROR_OPERATION_DISABLE,
    FW_ERROR_OPERATION_SET_CLOCK,
    FW_ERROR_OPERATION_GET_CLOCK,
} fw_error_operation_t;

/**
 * @brief Portable context for one failed firmware operation.
 *
 * instance identifies the resource within its category, such as a GPIO number,
 * SPI host number, or UART port number. detail is operation-specific portable
 * data, such as a requested byte count or clock frequency; it is never a raw
 * ESP-IDF error value.
 *
 * Functions accepting this type as an optional output clear it on success.
 * Passing NULL means that only the returned fw_status_t is required.
 */
typedef struct {
    fw_status_t status;
    fw_error_resource_t resource;
    fw_error_operation_t operation;
    uint32_t instance;
    uint32_t detail;
} fw_error_context_t;

#endif /* GEOPHYS_FW_ERROR_H */
