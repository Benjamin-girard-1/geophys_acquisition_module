#include "protocol_messages.h"

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

protocol_message_status_t protocol_decode_hello_request(
    const protocol_command_t *command)
{
    if (command == NULL) {
        return PROTOCOL_MESSAGE_INVALID_ARGUMENT;
    }
    if (command->command_id != PROTOCOL_COMMAND_HELLO) {
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
