#include "protocol_messages.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void write_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void write_u64_le(uint8_t *destination, uint64_t value)
{
    for (uint8_t index = 0U; index < 8U; index++) {
        destination[index] = (uint8_t)(value >> (8U * index));
    }
}

static uint16_t read_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static bool result_is_valid(protocol_command_result_t result)
{
    return result <= PROTOCOL_RESULT_LIMIT_REACHED;
}

static bool recording_name_is_valid(const protocol_recording_name_t *name,
                                    bool allow_empty)
{
    if (name == NULL) {
        return false;
    }
    if (name->bytes[0] == '\0') {
        if (!allow_empty) {
            return false;
        }
        for (size_t index = 1U;
             index < PROTOCOL_RECORDING_NAME_SIZE_BYTES;
             index++) {
            if (name->bytes[index] != '\0') {
                return false;
            }
        }
        return true;
    }

    bool terminated = false;
    for (size_t index = 0U;
         index < PROTOCOL_RECORDING_NAME_SIZE_BYTES;
         index++) {
        const uint8_t value = (uint8_t)name->bytes[index];
        if (terminated) {
            if (value != 0U) {
                return false;
            }
            continue;
        }
        if (value == 0U) {
            terminated = true;
            continue;
        }
        if (!((value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
              (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
              value == (uint8_t)'_' || value == (uint8_t)'-')) {
            return false;
        }
    }
    return terminated;
}

static protocol_message_status_t decode_recording_name_request(
    const protocol_command_t *command,
    protocol_command_id_t expected_id,
    protocol_recording_name_t *name)
{
    if (command == NULL || name == NULL) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    memset(name, 0, sizeof(*name));
    if (command->command_id != expected_id) {
        return PROTOCOL_MESSAGE_UNEXPECTED_ID;
    }
    if (command->direction != PROTOCOL_DIRECTION_TO_DEVICE) {
        return PROTOCOL_MESSAGE_UNEXPECTED_DIRECTION;
    }
    if (command->payload_length != PROTOCOL_RECORDING_NAME_SIZE_BYTES) {
        return PROTOCOL_MESSAGE_UNEXPECTED_LENGTH;
    }

    bool terminated = false;
    for (size_t index = 0U;
         index < PROTOCOL_RECORDING_NAME_SIZE_BYTES;
         index++) {
        uint8_t value = command->payload[index];
        if (terminated) {
            if (value != 0U) {
                return PROTOCOL_MESSAGE_INVALID_FIELD;
            }
            continue;
        }
        if (value == 0U) {
            terminated = true;
            continue;
        }
        if (value >= (uint8_t)'A' && value <= (uint8_t)'Z') {
            value = (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (!((value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
              (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
              value == (uint8_t)'_' || value == (uint8_t)'-')) {
            return PROTOCOL_MESSAGE_INVALID_FIELD;
        }
        name->bytes[index] = (char)value;
    }
    if (!terminated || name->bytes[0] == '\0') {
        memset(name, 0, sizeof(*name));
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    return PROTOCOL_MESSAGE_OK;
}

static protocol_message_status_t encode_reply(
    protocol_command_id_t command_id,
    const uint8_t *payload,
    uint8_t payload_size,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (payload == NULL || payload_size > PROTOCOL_COMMAND_PAYLOAD_SIZE_BYTES ||
        crc32 == NULL || frame == NULL) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    protocol_command_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.command_id = command_id;
    reply.direction = PROTOCOL_DIRECTION_TO_HOST;
    reply.payload_length = payload_size;
    memcpy(reply.payload, payload, payload_size);
    return (protocol_command_encode(&reply, crc32, crc_context, frame) ==
            PROTOCOL_FRAME_OK) ?
           PROTOCOL_MESSAGE_OK : PROTOCOL_MESSAGE_INVALID_ARGUMENT;
}

static bool boolean_is_valid(uint8_t value)
{
    return value <= UINT8_C(1);
}

static bool sample_rate_is_valid(uint8_t value)
{
    return value <= PROTOCOL_ADC_SAMPLE_RATE_16000_SPS;
}

static bool channel_mask_is_valid(uint8_t value)
{
    return value == PROTOCOL_ADC_CHANNEL_MASK_NONE ||
           value == PROTOCOL_ADC_CHANNEL_MASK_LOW ||
           value == PROTOCOL_ADC_CHANNEL_MASK_HIGH ||
           value == PROTOCOL_ADC_CHANNEL_MASK_ALL;
}

static bool streaming_decimation_is_valid(uint8_t value)
{
    return value == PROTOCOL_STREAMING_DECIMATION_NONE ||
           value == PROTOCOL_STREAMING_DECIMATION_2 ||
           value == PROTOCOL_STREAMING_DECIMATION_4 ||
           value == PROTOCOL_STREAMING_DECIMATION_5 ||
           value == PROTOCOL_STREAMING_DECIMATION_10 ||
           value == PROTOCOL_STREAMING_DECIMATION_20;
}

static bool config_update_is_valid(
    const protocol_device_config_update_t *update)
{
    return sample_rate_is_valid(update->adc_sample_rate) &&
           channel_mask_is_valid(update->adc_channel_mask) &&
           boolean_is_valid(update->rail_3v3_enabled) &&
           boolean_is_valid(update->rail_5v_enabled) &&
           boolean_is_valid(update->rail_9v_enabled) &&
           boolean_is_valid(update->rail_negative_5v_enabled) &&
           boolean_is_valid(update->rail_18v_enabled);
}

static bool device_config_is_valid(
    const protocol_device_config_t *config)
{
    return config->result <= PROTOCOL_RESULT_LIMIT_REACHED &&
           boolean_is_valid(config->recording_in_progress) &&
           config->card_slot_1 <= PROTOCOL_CARD_ACC_GEOPH &&
           config->card_slot_2 <= PROTOCOL_CARD_ACC_GEOPH &&
           sample_rate_is_valid(config->adc_sample_rate) &&
           channel_mask_is_valid(config->adc_channel_mask) &&
           boolean_is_valid(config->rail_3v3_enabled) &&
           boolean_is_valid(config->rail_5v_enabled) &&
           boolean_is_valid(config->rail_9v_enabled) &&
           boolean_is_valid(config->rail_negative_5v_enabled) &&
           boolean_is_valid(config->rail_18v_enabled) &&
           boolean_is_valid(config->solar_present) &&
           boolean_is_valid(config->usb_5v_present) &&
           config->gnss_state <= PROTOCOL_GNSS_SEARCHING &&
           config->imu_state <= PROTOCOL_IMU_FAULTED &&
           config->sd_card_state <= PROTOCOL_SD_CARD_FAULTED &&
           boolean_is_valid(config->error_pending);
}

static protocol_message_status_t decode_empty_request(
    const protocol_command_t *command,
    protocol_command_id_t expected_id)
{
    if (command == NULL) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    if (command->command_id != expected_id) {
        return PROTOCOL_MESSAGE_UNEXPECTED_ID;
    }
    if (command->direction != PROTOCOL_DIRECTION_TO_DEVICE) {
        return PROTOCOL_MESSAGE_UNEXPECTED_DIRECTION;
    }
    if (command->payload_length != 0U) {
        return PROTOCOL_MESSAGE_UNEXPECTED_LENGTH;
    }
    return PROTOCOL_MESSAGE_OK;
}

protocol_message_status_t protocol_decode_hello_request(
    const protocol_command_t *command)
{
    return decode_empty_request(command, PROTOCOL_COMMAND_HELLO);
}

protocol_message_status_t protocol_encode_device_info_reply(
    const protocol_device_info_t *device_info,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if ((device_info == NULL) || (crc32 == NULL) || (frame == NULL)) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    if (device_info->result > PROTOCOL_RESULT_LIMIT_REACHED) {
        return PROTOCOL_MESSAGE_INVALID_RESULT;
    }

    protocol_command_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.command_id = PROTOCOL_REPLY_DEVICE_INFO;
    reply.direction = PROTOCOL_DIRECTION_TO_HOST;
    reply.payload_length = PROTOCOL_DEVICE_INFO_PAYLOAD_SIZE_BYTES;
    reply.payload[0] = device_info->result;
    memcpy(reply.payload + 1U,
           device_info->mac_address,
           PROTOCOL_DEVICE_MAC_SIZE_BYTES);
    write_u16_le(reply.payload + 7U, device_info->hardware_version);
    write_u16_le(reply.payload + 9U, device_info->hardware_revision);
    write_u32_le(reply.payload + 11U, device_info->firmware_version);
    reply.payload[15] = device_info->protocol_version;

    return (protocol_command_encode(
                &reply, crc32, crc_context, frame) == PROTOCOL_FRAME_OK) ?
           PROTOCOL_MESSAGE_OK : PROTOCOL_MESSAGE_INVALID_ARGUMENT;
}

protocol_message_status_t protocol_decode_device_get_config_request(
    const protocol_command_t *command)
{
    return decode_empty_request(command, PROTOCOL_COMMAND_DEVICE_GET_CONFIG);
}

protocol_message_status_t protocol_decode_device_set_config_request(
    const protocol_command_t *command,
    protocol_device_config_update_t *update)
{
    if ((command == NULL) || (update == NULL)) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    memset(update, 0, sizeof(*update));
    if (command->command_id != PROTOCOL_COMMAND_DEVICE_SET_CONFIG) {
        return PROTOCOL_MESSAGE_UNEXPECTED_ID;
    }
    if (command->direction != PROTOCOL_DIRECTION_TO_DEVICE) {
        return PROTOCOL_MESSAGE_UNEXPECTED_DIRECTION;
    }
    if (command->payload_length !=
        PROTOCOL_DEVICE_SET_CONFIG_PAYLOAD_SIZE_BYTES) {
        return PROTOCOL_MESSAGE_UNEXPECTED_LENGTH;
    }

    update->adc_sample_rate = command->payload[12];
    update->adc_channel_mask = command->payload[13];
    update->adc_gain = read_u16_le(command->payload + 14U);
    update->rail_3v3_enabled = command->payload[18];
    update->rail_5v_enabled = command->payload[19];
    update->rail_9v_enabled = command->payload[20];
    update->rail_negative_5v_enabled = command->payload[21];
    update->rail_18v_enabled = command->payload[22];
    update->imu_averaging_time_ms = read_u16_le(command->payload + 28U);

    return config_update_is_valid(update) ?
           PROTOCOL_MESSAGE_OK : PROTOCOL_MESSAGE_INVALID_FIELD;
}

protocol_message_status_t protocol_encode_device_config_reply(
    const protocol_device_config_t *device_config,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if ((device_config == NULL) || (crc32 == NULL) || (frame == NULL)) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    if (!device_config_is_valid(device_config)) {
        return (device_config->result > PROTOCOL_RESULT_LIMIT_REACHED) ?
               PROTOCOL_MESSAGE_INVALID_RESULT :
               PROTOCOL_MESSAGE_INVALID_FIELD;
    }

    protocol_command_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.command_id = PROTOCOL_REPLY_DEVICE_CONFIG;
    reply.direction = PROTOCOL_DIRECTION_TO_HOST;
    reply.payload_length = PROTOCOL_DEVICE_CONFIG_PAYLOAD_SIZE_BYTES;
    reply.payload[0] = device_config->result;
    write_u64_le(reply.payload + 1U, device_config->timestamp_100ns);
    reply.payload[9] = device_config->recording_in_progress;
    reply.payload[10] = device_config->card_slot_1;
    reply.payload[11] = device_config->card_slot_2;
    reply.payload[12] = device_config->adc_sample_rate;
    reply.payload[13] = device_config->adc_channel_mask;
    write_u16_le(reply.payload + 14U, device_config->adc_gain);
    write_u16_le(reply.payload + 16U,
                 (uint16_t)device_config->adc_temperature_centi_c);
    reply.payload[18] = device_config->rail_3v3_enabled;
    reply.payload[19] = device_config->rail_5v_enabled;
    reply.payload[20] = device_config->rail_9v_enabled;
    reply.payload[21] = device_config->rail_negative_5v_enabled;
    reply.payload[22] = device_config->rail_18v_enabled;
    reply.payload[23] = device_config->solar_present;
    reply.payload[24] = device_config->usb_5v_present;
    reply.payload[25] = device_config->gnss_state;
    reply.payload[26] = device_config->gnss_satellite_count;
    reply.payload[27] = device_config->imu_state;
    write_u16_le(reply.payload + 28U,
                 device_config->imu_averaging_time_ms);
    write_u16_le(reply.payload + 30U,
                 (uint16_t)device_config->imu_roll_centi_degrees);
    write_u16_le(reply.payload + 32U,
                 (uint16_t)device_config->imu_pitch_centi_degrees);
    write_u16_le(reply.payload + 34U,
                 (uint16_t)device_config->imu_temperature_centi_c);
    reply.payload[36] = device_config->sd_card_state;
    write_u16_le(reply.payload + 37U,
                 (uint16_t)device_config->esp32_temperature_centi_c);
    reply.payload[39] = device_config->error_pending;

    return (protocol_command_encode(
                &reply, crc32, crc_context, frame) == PROTOCOL_FRAME_OK) ?
           PROTOCOL_MESSAGE_OK : PROTOCOL_MESSAGE_INVALID_ARGUMENT;
}

protocol_message_status_t protocol_decode_streaming_start_request(
    const protocol_command_t *command,
    protocol_streaming_start_request_t *request)
{
    if (command == NULL || request == NULL) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    memset(request, 0, sizeof(*request));
    if (command->command_id != PROTOCOL_COMMAND_STREAMING_START) {
        return PROTOCOL_MESSAGE_UNEXPECTED_ID;
    }
    if (command->direction != PROTOCOL_DIRECTION_TO_DEVICE) {
        return PROTOCOL_MESSAGE_UNEXPECTED_DIRECTION;
    }
    if (command->payload_length !=
        PROTOCOL_STREAMING_START_PAYLOAD_SIZE_BYTES) {
        return PROTOCOL_MESSAGE_UNEXPECTED_LENGTH;
    }

    request->decimation = command->payload[0];
    request->channel_mask = command->payload[1];
    return (streaming_decimation_is_valid(request->decimation) &&
            channel_mask_is_valid(request->channel_mask)) ?
           PROTOCOL_MESSAGE_OK : PROTOCOL_MESSAGE_INVALID_FIELD;
}

protocol_message_status_t protocol_encode_streaming_start_reply(
    const protocol_streaming_start_result_t *result,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (result == NULL || !result_is_valid(result->result) ||
        !streaming_decimation_is_valid(result->decimation) ||
        !channel_mask_is_valid(result->channel_mask) ||
        !boolean_is_valid(result->recording_in_progress)) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    const uint8_t payload[
        PROTOCOL_STREAMING_START_REPLY_PAYLOAD_SIZE_BYTES] = {
            result->result,
            result->decimation,
            result->channel_mask,
            result->recording_in_progress,
        };
    return encode_reply(PROTOCOL_REPLY_STREAMING_START_RESULT,
                        payload, sizeof(payload), crc32, crc_context, frame);
}

protocol_message_status_t protocol_decode_streaming_stop_request(
    const protocol_command_t *command)
{
    return decode_empty_request(command, PROTOCOL_COMMAND_STREAMING_STOP);
}

protocol_message_status_t protocol_encode_streaming_stop_reply(
    const protocol_streaming_stop_result_t *result,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (result == NULL || !result_is_valid(result->result) ||
        !boolean_is_valid(result->recording_in_progress)) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    const uint8_t payload[
        PROTOCOL_STREAMING_STOP_REPLY_PAYLOAD_SIZE_BYTES] = {
            result->result,
            result->recording_in_progress,
        };
    return encode_reply(PROTOCOL_REPLY_STREAMING_STOP_RESULT,
                        payload, sizeof(payload), crc32, crc_context, frame);
}

protocol_message_status_t protocol_decode_recording_start_request(
    const protocol_command_t *command,
    protocol_recording_name_t *name)
{
    return decode_recording_name_request(
        command, PROTOCOL_COMMAND_RECORDING_START, name);
}

protocol_message_status_t protocol_encode_recording_start_reply(
    const protocol_recording_start_result_t *result,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (result == NULL || !result_is_valid(result->result) ||
        !boolean_is_valid(result->recording_in_progress) ||
        !recording_name_is_valid(
            &result->name, result->result != PROTOCOL_RESULT_SUCCESS)) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    uint8_t payload[PROTOCOL_RECORDING_START_REPLY_PAYLOAD_SIZE_BYTES] = {0};
    payload[0] = result->result;
    payload[1] = result->recording_in_progress;
    memcpy(payload + 2U, result->name.bytes, sizeof(result->name.bytes));
    return encode_reply(PROTOCOL_REPLY_RECORDING_START_RESULT,
                        payload, sizeof(payload), crc32, crc_context, frame);
}

protocol_message_status_t protocol_decode_recording_stop_request(
    const protocol_command_t *command)
{
    return decode_empty_request(command, PROTOCOL_COMMAND_RECORDING_STOP);
}

protocol_message_status_t protocol_encode_recording_stop_reply(
    const protocol_recording_stop_result_t *result,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (result == NULL || !result_is_valid(result->result) ||
        !recording_name_is_valid(
            &result->name, result->result != PROTOCOL_RESULT_SUCCESS)) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    uint8_t payload[PROTOCOL_RECORDING_STOP_REPLY_PAYLOAD_SIZE_BYTES] = {0};
    payload[0] = result->result;
    memcpy(payload + 1U, result->name.bytes, sizeof(result->name.bytes));
    return encode_reply(PROTOCOL_REPLY_RECORDING_STOP_RESULT,
                        payload, sizeof(payload), crc32, crc_context, frame);
}

protocol_message_status_t protocol_decode_recording_get_number_request(
    const protocol_command_t *command)
{
    return decode_empty_request(
        command, PROTOCOL_COMMAND_RECORDING_GET_NUMBER);
}

protocol_message_status_t protocol_encode_recording_number_reply(
    const protocol_recording_number_t *number,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (number == NULL || !result_is_valid(number->result) ||
        number->recording_count > PROTOCOL_RECORDING_MAX_COUNT) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    uint8_t payload[PROTOCOL_RECORDING_NUMBER_PAYLOAD_SIZE_BYTES] = {0};
    payload[0] = number->result;
    write_u16_le(payload + 1U, number->recording_count);
    return encode_reply(PROTOCOL_REPLY_RECORDING_NUMBER,
                        payload, sizeof(payload), crc32, crc_context, frame);
}

protocol_message_status_t protocol_decode_recording_get_info_request(
    const protocol_command_t *command,
    uint16_t *recording_index)
{
    if (command == NULL || recording_index == NULL) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    *recording_index = 0U;
    if (command->command_id != PROTOCOL_COMMAND_RECORDING_GET_INFO) {
        return PROTOCOL_MESSAGE_UNEXPECTED_ID;
    }
    if (command->direction != PROTOCOL_DIRECTION_TO_DEVICE) {
        return PROTOCOL_MESSAGE_UNEXPECTED_DIRECTION;
    }
    if (command->payload_length !=
        PROTOCOL_RECORDING_INFO_REQUEST_PAYLOAD_SIZE_BYTES) {
        return PROTOCOL_MESSAGE_UNEXPECTED_LENGTH;
    }
    *recording_index = read_u16_le(command->payload);
    return PROTOCOL_MESSAGE_OK;
}

protocol_message_status_t protocol_encode_recording_info_reply(
    const protocol_recording_info_t *info,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (info == NULL || !result_is_valid(info->result) ||
        !boolean_is_valid(info->recording_in_progress)) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    if (info->result == PROTOCOL_RESULT_SUCCESS &&
        !recording_name_is_valid(&info->name, false)) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    uint8_t payload[PROTOCOL_RECORDING_INFO_PAYLOAD_SIZE_BYTES] = {0};
    payload[0] = info->result;
    write_u16_le(payload + 1U, info->recording_index);
    payload[3] = info->recording_in_progress;
    memcpy(payload + 4U, info->name.bytes, sizeof(info->name.bytes));
    write_u64_le(payload + 36U, info->start_unix_timestamp_us);
    write_u32_le(payload + 44U, info->size_bytes);
    return encode_reply(PROTOCOL_REPLY_RECORDING_INFO,
                        payload, sizeof(payload), crc32, crc_context, frame);
}

protocol_message_status_t protocol_decode_recording_delete_request(
    const protocol_command_t *command,
    protocol_recording_name_t *name)
{
    return decode_recording_name_request(
        command, PROTOCOL_COMMAND_RECORDING_DELETE, name);
}

protocol_message_status_t protocol_encode_recording_delete_reply(
    const protocol_recording_delete_result_t *result,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (result == NULL || !result_is_valid(result->result) ||
        !boolean_is_valid(result->recording_in_progress) ||
        !recording_name_is_valid(
            &result->name, result->result != PROTOCOL_RESULT_SUCCESS)) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    uint8_t payload[PROTOCOL_RECORDING_DELETE_REPLY_PAYLOAD_SIZE_BYTES] = {0};
    payload[0] = result->result;
    payload[1] = result->recording_in_progress;
    memcpy(payload + 2U, result->name.bytes, sizeof(result->name.bytes));
    return encode_reply(PROTOCOL_REPLY_RECORDING_DELETE_RESULT,
                        payload, sizeof(payload), crc32, crc_context, frame);
}

protocol_message_status_t protocol_decode_temp_recording_read_request(
    const protocol_command_t *command,
    protocol_temp_recording_read_request_t *request)
{
    if (command == NULL || request == NULL) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    memset(request, 0, sizeof(*request));
    if (command->command_id != PROTOCOL_COMMAND_TEMP_RECORDING_READ) {
        return PROTOCOL_MESSAGE_UNEXPECTED_ID;
    }
    if (command->direction != PROTOCOL_DIRECTION_TO_DEVICE) {
        return PROTOCOL_MESSAGE_UNEXPECTED_DIRECTION;
    }
    if (command->payload_length !=
        PROTOCOL_TEMP_RECORDING_READ_REQUEST_PAYLOAD_SIZE_BYTES) {
        return PROTOCOL_MESSAGE_UNEXPECTED_LENGTH;
    }

    protocol_command_t name_command = *command;
    name_command.payload_length = PROTOCOL_RECORDING_NAME_SIZE_BYTES;
    const protocol_message_status_t status = decode_recording_name_request(
        &name_command, PROTOCOL_COMMAND_TEMP_RECORDING_READ, &request->name);
    if (status != PROTOCOL_MESSAGE_OK) {
        return status;
    }
    request->offset_bytes = read_u32_le(
        command->payload + PROTOCOL_RECORDING_NAME_SIZE_BYTES);
    return PROTOCOL_MESSAGE_OK;
}

protocol_message_status_t protocol_encode_temp_recording_read_reply(
    const protocol_temp_recording_read_reply_t *result,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if (result == NULL || !result_is_valid(result->result) ||
        result->data_length_bytes >
            PROTOCOL_TEMP_RECORDING_READ_DATA_SIZE_BYTES) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }
    if (result->result == PROTOCOL_RESULT_SUCCESS) {
        if (result->offset_bytes > result->file_size_bytes ||
            result->data_length_bytes >
                result->file_size_bytes - result->offset_bytes) {
            return PROTOCOL_MESSAGE_INVALID_FIELD;
        }
    } else if (result->file_size_bytes != 0U ||
               result->data_length_bytes != 0U) {
        return PROTOCOL_MESSAGE_INVALID_FIELD;
    }

    uint8_t payload[
        PROTOCOL_TEMP_RECORDING_READ_REPLY_PAYLOAD_SIZE_BYTES] = {0};
    payload[0] = result->result;
    write_u32_le(payload + 1U, result->file_size_bytes);
    write_u32_le(payload + 5U, result->offset_bytes);
    payload[9] = result->data_length_bytes;
    memcpy(payload + 10U, result->data, result->data_length_bytes);
    return encode_reply(PROTOCOL_REPLY_TEMP_RECORDING_READ,
                        payload, sizeof(payload), crc32, crc_context, frame);
}
