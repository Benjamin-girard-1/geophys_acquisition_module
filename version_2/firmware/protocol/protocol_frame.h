#ifndef GEOPHYS_PROTOCOL_FRAME_H
#define GEOPHYS_PROTOCOL_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_COMMAND_SIZE_BYTES           UINT16_C(64)
#define PROTOCOL_COMMAND_HEADER_SIZE_BYTES    UINT16_C(12)
#define PROTOCOL_COMMAND_PAYLOAD_SIZE_BYTES   UINT16_C(48)
#define PROTOCOL_COMMAND_CRC_SIZE_BYTES       UINT16_C(4)
#define PROTOCOL_COMMAND_MAGIC_OFFSET_BYTES   UINT16_C(0)
#define PROTOCOL_COMMAND_MAGIC_SIZE_BYTES     UINT16_C(4)
#define PROTOCOL_COMMAND_MAGIC_BYTE_0         UINT8_C(0x5C)
#define PROTOCOL_COMMAND_MAGIC_BYTE_1         UINT8_C(0x43)
#define PROTOCOL_COMMAND_MAGIC_BYTE_2         UINT8_C(0x4D)
#define PROTOCOL_COMMAND_MAGIC_BYTE_3         UINT8_C(0x44)
#define PROTOCOL_COMMAND_ID_OFFSET_BYTES      UINT16_C(4)
#define PROTOCOL_COMMAND_DIRECTION_OFFSET_BYTES UINT16_C(6)
#define PROTOCOL_COMMAND_RESERVED_OFFSET_BYTES UINT16_C(7)
#define PROTOCOL_COMMAND_RESERVED_SIZE_BYTES  UINT16_C(4)
#define PROTOCOL_COMMAND_LENGTH_OFFSET_BYTES  UINT16_C(11)
#define PROTOCOL_COMMAND_PAYLOAD_OFFSET_BYTES UINT16_C(12)
#define PROTOCOL_COMMAND_CRC_OFFSET_BYTES     UINT16_C(60)

/** Compute a finalized CRC-32/ISO-HDLC value for one contiguous byte range. */
typedef uint32_t (*protocol_crc32_callback_t)(
    void *context,
    const uint8_t *data,
    size_t length_bytes);

typedef enum {
    PROTOCOL_FRAME_OK = 0,
    PROTOCOL_FRAME_INVALID_ARGUMENT,
    PROTOCOL_FRAME_INVALID_MAGIC,
    PROTOCOL_FRAME_INVALID_CRC,
    PROTOCOL_FRAME_INVALID_DIRECTION,
    PROTOCOL_FRAME_INVALID_RESERVED,
    PROTOCOL_FRAME_INVALID_LENGTH,
    PROTOCOL_FRAME_INVALID_PADDING,
} protocol_frame_status_t;

/** Logical command fields; this is not a wire-layout C structure. */
typedef struct {
    uint16_t command_id;
    uint8_t direction;
    uint8_t payload_length;
    uint8_t payload[PROTOCOL_COMMAND_PAYLOAD_SIZE_BYTES];
} protocol_command_t;

protocol_frame_status_t protocol_command_encode(
    const protocol_command_t *command,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES]);

protocol_frame_status_t protocol_command_decode(
    const uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES],
    protocol_crc32_callback_t crc32,
    void *crc_context,
    protocol_command_t *command);

typedef void (*protocol_parser_event_callback_t)(
    void *context,
    protocol_frame_status_t status,
    const protocol_command_t *command);

/** Allocation-free parser state for an arbitrarily fragmented byte stream. */
typedef struct {
    uint8_t candidate[PROTOCOL_COMMAND_SIZE_BYTES];
    size_t candidate_length;
    protocol_crc32_callback_t crc32;
    void *crc_context;
} protocol_command_parser_t;

protocol_frame_status_t protocol_command_parser_initialize(
    protocol_command_parser_t *parser,
    protocol_crc32_callback_t crc32,
    void *crc_context);

/**
 * Feed bytes into the parser and report complete valid or invalid candidates.
 *
 * The return value is the number of complete candidates reported through the
 * callback. Bytes before a synchronization word are discarded.
 */
size_t protocol_command_parser_feed(
    protocol_command_parser_t *parser,
    const uint8_t *data,
    size_t length_bytes,
    protocol_parser_event_callback_t event_callback,
    void *event_context);

#endif /* GEOPHYS_PROTOCOL_FRAME_H */
