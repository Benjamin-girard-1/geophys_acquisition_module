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

static protocol_command_result_t protocol_result_from_status(
    fw_status_t status)
{
    switch (status) {
    case FW_STATUS_OK:
        return PROTOCOL_RESULT_SUCCESS;
    case FW_STATUS_INVALID_ARGUMENT:
        return PROTOCOL_RESULT_INVALID_ARGUMENT;
    case FW_STATUS_INVALID_STATE:
        return PROTOCOL_RESULT_INVALID_STATE;
    case FW_STATUS_NOT_INITIALIZED:
        return PROTOCOL_RESULT_NOT_READY;
    case FW_STATUS_NOT_FOUND:
        return PROTOCOL_RESULT_NOT_FOUND;
    case FW_STATUS_BUSY:
        return PROTOCOL_RESULT_BUSY;
    case FW_STATUS_TIMEOUT:
        return PROTOCOL_RESULT_TIMEOUT;
    case FW_STATUS_IO:
        return PROTOCOL_RESULT_IO_ERROR;
    case FW_STATUS_INTEGRITY:
        return PROTOCOL_RESULT_INTEGRITY_ERROR;
    case FW_STATUS_OVERFLOW:
        return PROTOCOL_RESULT_LIMIT_REACHED;
    case FW_STATUS_UNSUPPORTED:
        return PROTOCOL_RESULT_UNSUPPORTED;
    case FW_STATUS_HARDWARE_FAULT:
        return PROTOCOL_RESULT_HARDWARE_FAULT;
    case FW_STATUS_INTERNAL:
    default:
        return PROTOCOL_RESULT_INTERNAL_ERROR;
    }
}

static bool decode_sample_rate(uint8_t encoded, uint32_t *sample_rate_sps)
{
    if (sample_rate_sps == NULL) {
        return false;
    }
    switch (encoded) {
    case PROTOCOL_ADC_SAMPLE_RATE_500_SPS:
        *sample_rate_sps = UINT32_C(500);
        return true;
    case PROTOCOL_ADC_SAMPLE_RATE_1000_SPS:
        *sample_rate_sps = UINT32_C(1000);
        return true;
    case PROTOCOL_ADC_SAMPLE_RATE_2000_SPS:
        *sample_rate_sps = UINT32_C(2000);
        return true;
    case PROTOCOL_ADC_SAMPLE_RATE_4000_SPS:
        *sample_rate_sps = UINT32_C(4000);
        return true;
    case PROTOCOL_ADC_SAMPLE_RATE_8000_SPS:
        *sample_rate_sps = UINT32_C(8000);
        return true;
    case PROTOCOL_ADC_SAMPLE_RATE_16000_SPS:
        *sample_rate_sps = UINT32_C(16000);
        return true;
    default:
        return false;
    }
}

static bool encode_sample_rate(uint32_t sample_rate_sps, uint8_t *encoded)
{
    if (encoded == NULL) {
        return false;
    }
    switch (sample_rate_sps) {
    case UINT32_C(500):
        *encoded = PROTOCOL_ADC_SAMPLE_RATE_500_SPS;
        return true;
    case UINT32_C(1000):
        *encoded = PROTOCOL_ADC_SAMPLE_RATE_1000_SPS;
        return true;
    case UINT32_C(2000):
        *encoded = PROTOCOL_ADC_SAMPLE_RATE_2000_SPS;
        return true;
    case UINT32_C(4000):
        *encoded = PROTOCOL_ADC_SAMPLE_RATE_4000_SPS;
        return true;
    case UINT32_C(8000):
        *encoded = PROTOCOL_ADC_SAMPLE_RATE_8000_SPS;
        return true;
    case UINT32_C(16000):
        *encoded = PROTOCOL_ADC_SAMPLE_RATE_16000_SPS;
        return true;
    default:
        return false;
    }
}

static bool decode_gain(uint8_t encoded, uint8_t *gain)
{
    if (gain == NULL || encoded > UINT8_C(3)) {
        return false;
    }
    *gain = (uint8_t)(UINT8_C(1) << encoded);
    return true;
}

static bool encode_gain(uint8_t gain, uint8_t *encoded)
{
    if (encoded == NULL) {
        return false;
    }
    switch (gain) {
    case UINT8_C(1):
        *encoded = UINT8_C(0);
        return true;
    case UINT8_C(2):
        *encoded = UINT8_C(1);
        return true;
    case UINT8_C(4):
        *encoded = UINT8_C(2);
        return true;
    case UINT8_C(8):
        *encoded = UINT8_C(3);
        return true;
    default:
        return false;
    }
}

static bool device_snapshot_fields_are_valid(
    const device_configuration_snapshot_t *snapshot)
{
    const uint8_t channel_mask = snapshot->adc_channel_mask;
    return (channel_mask == PROTOCOL_ADC_CHANNEL_MASK_NONE ||
            channel_mask == PROTOCOL_ADC_CHANNEL_MASK_LOW ||
            channel_mask == PROTOCOL_ADC_CHANNEL_MASK_HIGH ||
            channel_mask == PROTOCOL_ADC_CHANNEL_MASK_ALL) &&
           snapshot->card_slots[0] <= DEVICE_CARD_ACC_GEOPH &&
           snapshot->card_slots[1] <= DEVICE_CARD_ACC_GEOPH &&
           snapshot->gnss_state <= DEVICE_GNSS_SEARCHING &&
           snapshot->imu_state <= DEVICE_IMU_FAULTED &&
           snapshot->sd_card_state <= DEVICE_SD_CARD_FAULTED;
}

static bool protocol_update_to_device(
    const protocol_device_config_update_t *source,
    device_configuration_update_t *destination)
{
    if (source == NULL || destination == NULL) {
        return false;
    }
    memset(destination, 0, sizeof(*destination));
    if (!decode_sample_rate(source->adc_sample_rate,
                            &destination->adc_sample_rate_sps)) {
        return false;
    }
    destination->adc_channel_mask = source->adc_channel_mask;
    for (uint8_t channel = 0U;
         channel < DEVICE_CONFIGURATION_ADC_CHANNEL_COUNT;
         channel++) {
        const uint8_t encoded = (uint8_t)(
            (source->adc_gain >> (2U * channel)) & UINT16_C(0x0003));
        if (!decode_gain(encoded, &destination->adc_gains[channel])) {
            return false;
        }
    }
    destination->rail_3v3_enabled = source->rail_3v3_enabled != 0U;
    destination->rail_5v_enabled = source->rail_5v_enabled != 0U;
    destination->rail_9v_enabled = source->rail_9v_enabled != 0U;
    destination->rail_negative_5v_enabled =
        source->rail_negative_5v_enabled != 0U;
    destination->rail_18v_enabled = source->rail_18v_enabled != 0U;
    destination->imu_averaging_time_ms = source->imu_averaging_time_ms;
    return true;
}

static bool device_snapshot_to_protocol(
    const device_configuration_snapshot_t *source,
    protocol_device_config_t *destination)
{
    if (source == NULL || destination == NULL ||
        !device_snapshot_fields_are_valid(source)) {
        return false;
    }
    memset(destination, 0, sizeof(*destination));
    if (!encode_sample_rate(source->adc_sample_rate_sps,
                            &destination->adc_sample_rate)) {
        return false;
    }
    destination->timestamp_100ns = source->timestamp_100ns;
    destination->recording_in_progress = source->recording_in_progress;
    destination->card_slot_1 = (uint8_t)source->card_slots[0];
    destination->card_slot_2 = (uint8_t)source->card_slots[1];
    destination->adc_channel_mask = source->adc_channel_mask;
    for (uint8_t channel = 0U;
         channel < DEVICE_CONFIGURATION_ADC_CHANNEL_COUNT;
         channel++) {
        uint8_t encoded = 0U;
        if (!encode_gain(source->adc_gains[channel], &encoded)) {
            return false;
        }
        destination->adc_gain |=
            (uint16_t)((uint16_t)encoded << (2U * channel));
    }
    destination->adc_temperature_centi_c =
        source->adc_temperature_centi_c;
    destination->rail_3v3_enabled = source->rail_3v3_enabled;
    destination->rail_5v_enabled = source->rail_5v_enabled;
    destination->rail_9v_enabled = source->rail_9v_enabled;
    destination->rail_negative_5v_enabled =
        source->rail_negative_5v_enabled;
    destination->rail_18v_enabled = source->rail_18v_enabled;
    destination->solar_present = source->solar_present;
    destination->usb_5v_present = source->usb_5v_present;
    destination->gnss_state = (uint8_t)source->gnss_state;
    destination->gnss_satellite_count = source->gnss_satellite_count;
    destination->imu_state = (uint8_t)source->imu_state;
    destination->imu_averaging_time_ms = source->imu_averaging_time_ms;
    destination->imu_roll_centi_degrees = source->imu_roll_centi_degrees;
    destination->imu_pitch_centi_degrees = source->imu_pitch_centi_degrees;
    destination->imu_temperature_centi_c =
        source->imu_temperature_centi_c;
    destination->sd_card_state = (uint8_t)source->sd_card_state;
    destination->esp32_temperature_centi_c =
        source->esp32_temperature_centi_c;
    destination->error_pending = source->error_pending;
    return true;
}

static void send_device_config_reply(protocol_command_result_t result)
{
    device_configuration_snapshot_t snapshot;
    protocol_device_config_t reply_config;
    memset(&snapshot, 0, sizeof(snapshot));
    memset(&reply_config, 0, sizeof(reply_config));

    const fw_status_t get_status =
        s_communication.config.get_device_config(&snapshot, NULL);
    if (get_status != FW_STATUS_OK) {
        result = protocol_result_from_status(get_status);
    } else if (!device_snapshot_to_protocol(&snapshot, &reply_config)) {
        result = PROTOCOL_RESULT_INTERNAL_ERROR;
        memset(&reply_config, 0, sizeof(reply_config));
    }
    reply_config.result = result;

    uint8_t reply[PROTOCOL_COMMAND_SIZE_BYTES];
    if (protocol_encode_device_config_reply(
            &reply_config,
            s_communication.config.crc32,
            s_communication.config.crc_context,
            reply) == PROTOCOL_MESSAGE_OK) {
        (void)write_complete_frame(reply);
    }
}

static void handle_device_get_config(const protocol_command_t *command)
{
    const protocol_message_status_t status =
        protocol_decode_device_get_config_request(command);
    send_device_config_reply(
        (status == PROTOCOL_MESSAGE_OK) ?
        PROTOCOL_RESULT_SUCCESS : PROTOCOL_RESULT_INVALID_ARGUMENT);
}

static void handle_device_set_config(const protocol_command_t *command)
{
    protocol_device_config_update_t protocol_update;
    device_configuration_update_t device_update;
    const protocol_message_status_t decode_status =
        protocol_decode_device_set_config_request(command, &protocol_update);
    if (decode_status != PROTOCOL_MESSAGE_OK ||
        !protocol_update_to_device(&protocol_update, &device_update)) {
        send_device_config_reply(PROTOCOL_RESULT_INVALID_ARGUMENT);
        return;
    }

    const fw_status_t apply_status =
        s_communication.config.apply_device_config(&device_update, NULL);
    send_device_config_reply(protocol_result_from_status(apply_status));
}

static void handle_parser_event(void *context,
                                protocol_frame_status_t status,
                                const protocol_command_t *command)
{
    (void)context;
    if ((status != PROTOCOL_FRAME_OK) || (command == NULL)) {
        return;
    }

    refresh_usb_activity(false);
    if (command->command_id == PROTOCOL_COMMAND_DEVICE_GET_CONFIG) {
        handle_device_get_config(command);
        return;
    }
    if (command->command_id == PROTOCOL_COMMAND_DEVICE_SET_CONFIG) {
        handle_device_set_config(command);
        return;
    }
    if (protocol_decode_hello_request(command) != PROTOCOL_MESSAGE_OK) {
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
        config->get_device_config == NULL ||
        config->apply_device_config == NULL ||
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
