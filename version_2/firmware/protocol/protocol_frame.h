#ifndef GEOPHYS_PROTOCOL_FRAME_H
#define GEOPHYS_PROTOCOL_FRAME_H

#include <stdint.h>

/* Compile-only scaffold for the command format in shared/protocol/protocol.md. */
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

/**
 * @brief Logical decoded command fields; never a wire-layout C structure.
 */
typedef struct {
    uint16_t command_id;
    uint8_t direction;
    uint8_t payload_length;
} protocol_command_fields_t;

/*
 * Planned against shared test vectors:
 * - fixed-command encoder and decoder;
 * - incremental, allocation-free stream parser;
 * - CRC and canonical-padding validation;
 * - explicit little-endian field access.
 */

#endif /* GEOPHYS_PROTOCOL_FRAME_H */
