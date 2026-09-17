#include "protocol_frame.h"

#include <stdbool.h>
#include <string.h>

static const uint8_t s_command_magic[PROTOCOL_COMMAND_MAGIC_SIZE_BYTES] = {
    PROTOCOL_COMMAND_MAGIC_BYTE_0,
    PROTOCOL_COMMAND_MAGIC_BYTE_1,
    PROTOCOL_COMMAND_MAGIC_BYTE_2,
    PROTOCOL_COMMAND_MAGIC_BYTE_3,
};

_Static_assert(
    PROTOCOL_COMMAND_MAGIC_OFFSET_BYTES +
        PROTOCOL_COMMAND_MAGIC_SIZE_BYTES ==
        PROTOCOL_COMMAND_ID_OFFSET_BYTES,
    "command ID must immediately follow command magic");

_Static_assert(
    PROTOCOL_COMMAND_PAYLOAD_OFFSET_BYTES ==
        PROTOCOL_COMMAND_HEADER_SIZE_BYTES,
    "command payload must immediately follow its header");

_Static_assert(
    PROTOCOL_COMMAND_HEADER_SIZE_BYTES +
        PROTOCOL_COMMAND_PAYLOAD_SIZE_BYTES +
        PROTOCOL_COMMAND_CRC_SIZE_BYTES ==
        PROTOCOL_COMMAND_SIZE_BYTES,
    "command regions must total 64 bytes");

_Static_assert(
    PROTOCOL_COMMAND_CRC_OFFSET_BYTES +
        PROTOCOL_COMMAND_CRC_SIZE_BYTES ==
        PROTOCOL_COMMAND_SIZE_BYTES,
    "command CRC must occupy the final four bytes");

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

static bool direction_is_valid(uint8_t direction)
{
    return (direction == UINT8_C(0x00)) ||
           (direction == UINT8_C(0x01));
}

protocol_frame_status_t protocol_command_encode(
    const protocol_command_t *command,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    if ((command == NULL) || (crc32 == NULL) || (frame == NULL)) {
        return PROTOCOL_FRAME_INVALID_ARGUMENT;
    }
    if (!direction_is_valid(command->direction)) {
        return PROTOCOL_FRAME_INVALID_DIRECTION;
    }
    if (command->payload_length > PROTOCOL_COMMAND_PAYLOAD_SIZE_BYTES) {
        return PROTOCOL_FRAME_INVALID_LENGTH;
    }

    memset(frame, 0, PROTOCOL_COMMAND_SIZE_BYTES);
    memcpy(frame + PROTOCOL_COMMAND_MAGIC_OFFSET_BYTES,
           s_command_magic, sizeof(s_command_magic));
    write_u16_le(frame + PROTOCOL_COMMAND_ID_OFFSET_BYTES,
                 command->command_id);
    frame[PROTOCOL_COMMAND_DIRECTION_OFFSET_BYTES] = command->direction;
    frame[PROTOCOL_COMMAND_LENGTH_OFFSET_BYTES] = command->payload_length;
    memcpy(frame + PROTOCOL_COMMAND_PAYLOAD_OFFSET_BYTES,
           command->payload, command->payload_length);

    const uint32_t crc = crc32(
        crc_context, frame, PROTOCOL_COMMAND_CRC_OFFSET_BYTES);
    write_u32_le(frame + PROTOCOL_COMMAND_CRC_OFFSET_BYTES, crc);
    return PROTOCOL_FRAME_OK;
}

protocol_frame_status_t protocol_command_decode(
    const uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES],
    protocol_crc32_callback_t crc32,
    void *crc_context,
    protocol_command_t *command)
{
    if ((frame == NULL) || (crc32 == NULL) || (command == NULL)) {
        return PROTOCOL_FRAME_INVALID_ARGUMENT;
    }
    memset(command, 0, sizeof(*command));

    if (memcmp(frame + PROTOCOL_COMMAND_MAGIC_OFFSET_BYTES,
               s_command_magic, sizeof(s_command_magic)) != 0) {
        return PROTOCOL_FRAME_INVALID_MAGIC;
    }

    const uint32_t expected_crc = read_u32_le(
        frame + PROTOCOL_COMMAND_CRC_OFFSET_BYTES);
    const uint32_t actual_crc = crc32(
        crc_context, frame, PROTOCOL_COMMAND_CRC_OFFSET_BYTES);
    if (actual_crc != expected_crc) {
        return PROTOCOL_FRAME_INVALID_CRC;
    }

    const uint8_t direction =
        frame[PROTOCOL_COMMAND_DIRECTION_OFFSET_BYTES];
    if (!direction_is_valid(direction)) {
        return PROTOCOL_FRAME_INVALID_DIRECTION;
    }

    for (size_t index = 0U;
         index < PROTOCOL_COMMAND_RESERVED_SIZE_BYTES;
         index++) {
        if (frame[PROTOCOL_COMMAND_RESERVED_OFFSET_BYTES + index] != 0U) {
            return PROTOCOL_FRAME_INVALID_RESERVED;
        }
    }

    const uint8_t payload_length =
        frame[PROTOCOL_COMMAND_LENGTH_OFFSET_BYTES];
    if (payload_length > PROTOCOL_COMMAND_PAYLOAD_SIZE_BYTES) {
        return PROTOCOL_FRAME_INVALID_LENGTH;
    }

    for (size_t index = payload_length;
         index < PROTOCOL_COMMAND_PAYLOAD_SIZE_BYTES;
         index++) {
        if (frame[PROTOCOL_COMMAND_PAYLOAD_OFFSET_BYTES + index] != 0U) {
            return PROTOCOL_FRAME_INVALID_PADDING;
        }
    }

    command->command_id = read_u16_le(
        frame + PROTOCOL_COMMAND_ID_OFFSET_BYTES);
    command->direction = direction;
    command->payload_length = payload_length;
    memcpy(command->payload,
           frame + PROTOCOL_COMMAND_PAYLOAD_OFFSET_BYTES,
           payload_length);
    return PROTOCOL_FRAME_OK;
}

protocol_frame_status_t protocol_command_parser_initialize(
    protocol_command_parser_t *parser,
    protocol_crc32_callback_t crc32,
    void *crc_context)
{
    if ((parser == NULL) || (crc32 == NULL)) {
        return PROTOCOL_FRAME_INVALID_ARGUMENT;
    }

    memset(parser, 0, sizeof(*parser));
    parser->crc32 = crc32;
    parser->crc_context = crc_context;
    return PROTOCOL_FRAME_OK;
}

static void parser_seek_magic(protocol_command_parser_t *parser,
                              uint8_t byte)
{
    if (byte == s_command_magic[parser->candidate_length]) {
        parser->candidate[parser->candidate_length] = byte;
        parser->candidate_length++;
        return;
    }

    if (byte == s_command_magic[0]) {
        parser->candidate[0] = byte;
        parser->candidate_length = 1U;
    } else {
        parser->candidate_length = 0U;
    }
}

static void parser_resynchronize_candidate(protocol_command_parser_t *parser)
{
    for (size_t start = 1U;
         start + PROTOCOL_COMMAND_MAGIC_SIZE_BYTES <=
             PROTOCOL_COMMAND_SIZE_BYTES;
         start++) {
        if (memcmp(parser->candidate + start,
                   s_command_magic,
                   sizeof(s_command_magic)) == 0) {
            const size_t retained = PROTOCOL_COMMAND_SIZE_BYTES - start;
            memmove(parser->candidate, parser->candidate + start, retained);
            parser->candidate_length = retained;
            return;
        }
    }

    size_t retained = 0U;
    for (size_t length = 1U;
         length < PROTOCOL_COMMAND_MAGIC_SIZE_BYTES;
         length++) {
        if (memcmp(parser->candidate +
                       PROTOCOL_COMMAND_SIZE_BYTES - length,
                   s_command_magic,
                   length) == 0) {
            retained = length;
        }
    }

    if (retained > 0U) {
        memmove(parser->candidate,
                parser->candidate + PROTOCOL_COMMAND_SIZE_BYTES - retained,
                retained);
    }
    parser->candidate_length = retained;
}

size_t protocol_command_parser_feed(
    protocol_command_parser_t *parser,
    const uint8_t *data,
    size_t length_bytes,
    protocol_parser_event_callback_t event_callback,
    void *event_context)
{
    if ((parser == NULL) || (parser->crc32 == NULL) ||
        ((data == NULL) && (length_bytes != 0U))) {
        return 0U;
    }

    size_t event_count = 0U;
    for (size_t index = 0U; index < length_bytes; index++) {
        if (parser->candidate_length <
            PROTOCOL_COMMAND_MAGIC_SIZE_BYTES) {
            parser_seek_magic(parser, data[index]);
            continue;
        }

        parser->candidate[parser->candidate_length] = data[index];
        parser->candidate_length++;
        if (parser->candidate_length < PROTOCOL_COMMAND_SIZE_BYTES) {
            continue;
        }

        protocol_command_t command;
        const protocol_frame_status_t status = protocol_command_decode(
            parser->candidate,
            parser->crc32,
            parser->crc_context,
            &command);
        if (event_callback != NULL) {
            event_callback(event_context, status,
                           (status == PROTOCOL_FRAME_OK) ? &command : NULL);
        }
        event_count++;

        if (status == PROTOCOL_FRAME_OK) {
            parser->candidate_length = 0U;
        } else {
            parser_resynchronize_candidate(parser);
        }
    }
    return event_count;
}
