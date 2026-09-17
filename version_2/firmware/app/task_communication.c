#include "task_communication.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_time.h"
#include "platform_time.h"
#include "protocol_frame.h"

#define TASK_COMMUNICATION_STACK_SIZE_BYTES UINT32_C(4096)
#define TASK_COMMUNICATION_PRIORITY (tskIDLE_PRIORITY + 1U)
#define TASK_COMMUNICATION_READ_BUFFER_SIZE_BYTES UINT32_C(128)
#define TASK_COMMUNICATION_USB_SESSION_TIMEOUT_100NS \
    (UINT64_C(5) * FW_MONOTONIC_FREQUENCY_HZ)
#define TASK_COMMUNICATION_ERROR_RETRY_MS UINT32_C(10)

typedef struct {
    task_communication_config_t config;
    protocol_command_parser_t parser;
    TaskHandle_t task_handle;
    fw_monotonic_100ns_t last_usb_activity_100ns;
    bool usb_session_active;
    bool started;
} task_communication_state_t;

static task_communication_state_t s_communication;

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
                             fw_error_operation_t operation,
                             uint32_t detail)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_SYNCHRONIZATION,
            .operation = operation,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = detail,
        };
    }
    return status;
}

static void refresh_usb_activity(bool establish_session)
{
    fw_monotonic_100ns_t timestamp;
    if (platform_monotonic_time_100ns(&timestamp, NULL) != FW_STATUS_OK) {
        return;
    }

    if (establish_session || s_communication.usb_session_active) {
        s_communication.usb_session_active = true;
        s_communication.last_usb_activity_100ns = timestamp;
    }
}

static void expire_usb_session_if_idle(void)
{
    if (!s_communication.usb_session_active) {
        return;
    }

    fw_monotonic_100ns_t timestamp;
    if (platform_monotonic_time_100ns(&timestamp, NULL) != FW_STATUS_OK) {
        return;
    }

    if ((timestamp - s_communication.last_usb_activity_100ns) >=
        TASK_COMMUNICATION_USB_SESSION_TIMEOUT_100NS) {
        s_communication.usb_session_active = false;
    }
}

static fw_status_t write_complete_frame(
    const uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    size_t offset = 0U;
    while (offset < PROTOCOL_COMMAND_SIZE_BYTES) {
        size_t bytes_written = 0U;
        const fw_status_t status =
            s_communication.config.transport.write_some(
                s_communication.config.transport.context,
                frame + offset,
                PROTOCOL_COMMAND_SIZE_BYTES - offset,
                &bytes_written,
                s_communication.config.write_timeout_us,
                NULL);
        offset += bytes_written;

        if (status == FW_STATUS_OK) {
            if (bytes_written == 0U) {
                return FW_STATUS_IO;
            }
            continue;
        }
        if ((status == FW_STATUS_TIMEOUT) && (bytes_written > 0U)) {
            continue;
        }
        return status;
    }
    return FW_STATUS_OK;
}

static void handle_parser_event(void *context,
                                protocol_frame_status_t status,
                                const protocol_command_t *command)
{
    (void)context;
    if ((status != PROTOCOL_FRAME_OK) || (command == NULL)) {
        return;
    }

    if (protocol_decode_hello_request(command) != PROTOCOL_MESSAGE_OK) {
        refresh_usb_activity(false);
        return;
    }

    uint8_t reply[PROTOCOL_COMMAND_SIZE_BYTES];
    if (protocol_encode_device_info_reply(
            &s_communication.config.device_info,
            s_communication.config.crc32,
            s_communication.config.crc_context,
            reply) != PROTOCOL_MESSAGE_OK) {
        return;
    }

    refresh_usb_activity(true);
    (void)write_complete_frame(reply);
}

static void task_communication_run(void *context)
{
    (void)context;
    uint8_t read_buffer[TASK_COMMUNICATION_READ_BUFFER_SIZE_BYTES];

    for (;;) {
        size_t bytes_read = 0U;
        const fw_status_t status =
            s_communication.config.transport.read_some(
                s_communication.config.transport.context,
                read_buffer,
                sizeof(read_buffer),
                &bytes_read,
                s_communication.config.read_timeout_us,
                NULL);

        if (bytes_read > 0U) {
            (void)protocol_command_parser_feed(
                &s_communication.parser,
                read_buffer,
                bytes_read,
                handle_parser_event,
                NULL);
        }

        expire_usb_session_if_idle();
        if ((status != FW_STATUS_OK) && (status != FW_STATUS_TIMEOUT)) {
            platform_delay_ms(TASK_COMMUNICATION_ERROR_RETRY_MS);
        }
    }
}

fw_status_t task_communication_start(
    const task_communication_config_t *config,
    fw_error_context_t *error)
{
    clear_error(error);
    if (config == NULL || config->transport.read_some == NULL ||
        config->transport.write_some == NULL || config->crc32 == NULL ||
        config->read_timeout_us == 0U || config->write_timeout_us == 0U) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    if (s_communication.started) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }

    memset(&s_communication, 0, sizeof(s_communication));
    s_communication.config = *config;
    if (protocol_command_parser_initialize(
            &s_communication.parser,
            config->crc32,
            config->crc_context) != PROTOCOL_FRAME_OK) {
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }

    if (xTaskCreate(
            task_communication_run,
            "communication",
            TASK_COMMUNICATION_STACK_SIZE_BYTES,
            NULL,
            TASK_COMMUNICATION_PRIORITY,
            &s_communication.task_handle) != pdPASS) {
        memset(&s_communication, 0, sizeof(s_communication));
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE,
                         TASK_COMMUNICATION_STACK_SIZE_BYTES);
    }

    s_communication.started = true;
    return FW_STATUS_OK;
}
