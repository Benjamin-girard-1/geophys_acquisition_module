#ifndef GEOPHYS_PROTOCOL_FRAME_H
#define GEOPHYS_PROTOCOL_FRAME_H

#include <stdint.h>

/*
 * Compile-only scaffold for the proposed fixed control-record format.
 *
 * No magic value, protocol version, message identifier, parser, encoder, or
 * decoder is approved or implemented yet. See shared/protocol/protocol_frame.md.
 */
#define PROTOCOL_CONTROL_RECORD_SIZE_BYTES    UINT16_C(128)
#define PROTOCOL_CONTROL_HEADER_SIZE_BYTES    UINT16_C(20)
#define PROTOCOL_CONTROL_PAYLOAD_SIZE_BYTES   UINT16_C(104)
#define PROTOCOL_CONTROL_CRC_SIZE_BYTES       UINT16_C(4)
#define PROTOCOL_CONTROL_CRC_OFFSET_BYTES     UINT16_C(124)

/**
 * @brief Logical decoded fields; this is never a wire-layout C structure.
 *
 * Numeric type, flag, and status values remain unassigned pending review.
 * Payload storage and ownership will be defined with the codec API.
 */
typedef struct {
    uint8_t protocol_version;
    uint8_t message_type;
    uint8_t flags;
    uint8_t payload_length;
    uint32_t message_sequence;
    uint32_t request_id;
    uint16_t status_code;
} protocol_control_fields_t;

/*
 * Planned after format approval:
 * - fixed-record encoder and decoder;
 * - incremental, allocation-free stream parser;
 * - CRC and canonical-padding validation;
 * - explicit little-endian field access.
 */

#endif /* GEOPHYS_PROTOCOL_FRAME_H */
