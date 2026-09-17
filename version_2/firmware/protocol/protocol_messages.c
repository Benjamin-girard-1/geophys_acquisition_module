#include "protocol_messages.h"

#include <stdbool.h>
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
