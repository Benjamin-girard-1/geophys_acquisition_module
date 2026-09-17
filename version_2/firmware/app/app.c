#include "app.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "platform_crc.h"
#include "platform_device.h"
#include "platform_uart.h"
#include "protocol_messages.h"
#include "task_communication.h"
#include "transport_uart.h"

#define APP_HOST_READ_TIMEOUT_US UINT32_C(100000)
#define APP_HOST_WRITE_TIMEOUT_US UINT32_C(100000)
#define APP_HOST_DEINITIALIZE_TIMEOUT_US UINT32_C(100000)

static platform_uart_t *s_host_uart;
static bool s_started;

static void clear_error(fw_error_context_t *error)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = FW_STATUS_OK,
            .resource = FW_ERROR_RESOURCE_NONE,
            .operation = FW_ERROR_OPERATION_NONE,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = 0U,
        };
    }
}

static uint32_t protocol_crc32(void *context,
                               const uint8_t *data,
                               size_t length_bytes)
{
    (void)context;
    return platform_crc32_le(
               UINT32_C(0xFFFFFFFF), data, length_bytes) ^
           UINT32_C(0xFFFFFFFF);
}

fw_status_t app_start(fw_error_context_t *error)
{
    clear_error(error);
    if (s_started) {
        return FW_STATUS_OK;
    }

    protocol_device_info_t device_info = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .hardware_version = BOARD_HARDWARE_VERSION,
        .hardware_revision = BOARD_HARDWARE_REVISION,
        .firmware_version = APP_FIRMWARE_VERSION,
        .protocol_version = PROTOCOL_VERSION_CURRENT,
    };
    fw_status_t status = platform_device_read_base_mac(
        device_info.mac_address, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = board_host_uart_initialize(&s_host_uart, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    const task_communication_config_t communication_config = {
        .transport = transport_uart_interface(s_host_uart),
        .crc32 = protocol_crc32,
        .crc_context = NULL,
        .device_info = device_info,
        .read_timeout_us = APP_HOST_READ_TIMEOUT_US,
        .write_timeout_us = APP_HOST_WRITE_TIMEOUT_US,
    };
    status = task_communication_start(&communication_config, error);
    if (status != FW_STATUS_OK) {
        fw_error_context_t original_error;
        if (error != NULL) {
            original_error = *error;
        }
        (void)platform_uart_deinitialize(
            s_host_uart, APP_HOST_DEINITIALIZE_TIMEOUT_US, NULL);
        s_host_uart = NULL;
        if (error != NULL) {
            *error = original_error;
        }
        return status;
    }

    s_started = true;
    return FW_STATUS_OK;
}
