#include "task_bluetooth.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "protocol_frame.h"
#include "transport_ble.h"

typedef struct {
    task_bluetooth_config_t config;
    protocol_command_parser_t parser;
    bool started;
} task_bluetooth_state_t;

static task_bluetooth_state_t s_bluetooth;

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

static fw_status_t set_error(fw_error_context_t *error,
                             fw_status_t status,
                             fw_error_operation_t operation)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_BLUETOOTH,
            .operation = operation,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = 0U,
        };
    }
    return status;
}

static void handle_command(void *context,
                           protocol_frame_status_t status,
                           const protocol_command_t *command)
{
    (void)context;
    if (status != PROTOCOL_FRAME_OK || command == NULL ||
        protocol_decode_hello_request(command) != PROTOCOL_MESSAGE_OK) {
        return;
    }

    uint8_t reply[PROTOCOL_COMMAND_SIZE_BYTES];
    if (protocol_encode_device_info_reply(
            &s_bluetooth.config.device_info,
            s_bluetooth.config.crc32,
            s_bluetooth.config.crc_context,
            reply) == PROTOCOL_MESSAGE_OK) {
        (void)transport_ble_notify(reply, sizeof(reply), NULL);
    }
}

static void receive_bytes(void *context,
                          const uint8_t *data,
                          size_t length_bytes)
{
    (void)context;
    (void)protocol_command_parser_feed(
        &s_bluetooth.parser,
        data,
        length_bytes,
        handle_command,
        NULL);
}

fw_status_t task_bluetooth_start(const task_bluetooth_config_t *config,
                                 fw_error_context_t *error)
{
    clear_error(error);
    if (config == NULL || config->crc32 == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_INITIALIZE);
    }
    if (s_bluetooth.started) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_INITIALIZE);
    }

    memset(&s_bluetooth, 0, sizeof(s_bluetooth));
    s_bluetooth.config = *config;
    if (protocol_command_parser_initialize(
            &s_bluetooth.parser,
            config->crc32,
            config->crc_context) != PROTOCOL_FRAME_OK) {
        memset(&s_bluetooth, 0, sizeof(s_bluetooth));
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE);
    }

    const transport_ble_config_t transport_config = {
        .receive = receive_bytes,
        .receive_context = NULL,
    };
    const fw_status_t status = transport_ble_start(
        &transport_config, error);
    if (status != FW_STATUS_OK) {
        memset(&s_bluetooth, 0, sizeof(s_bluetooth));
        return status;
    }

    s_bluetooth.started = true;
    return FW_STATUS_OK;
}
