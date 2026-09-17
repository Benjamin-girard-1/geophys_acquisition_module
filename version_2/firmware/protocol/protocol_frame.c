#include "protocol_frame.h"

_Static_assert(
    PROTOCOL_CONTROL_HEADER_SIZE_BYTES +
        PROTOCOL_CONTROL_PAYLOAD_SIZE_BYTES +
        PROTOCOL_CONTROL_CRC_SIZE_BYTES ==
        PROTOCOL_CONTROL_RECORD_SIZE_BYTES,
    "proposed control-record regions must total 128 bytes");

_Static_assert(
    PROTOCOL_CONTROL_CRC_OFFSET_BYTES +
        PROTOCOL_CONTROL_CRC_SIZE_BYTES ==
        PROTOCOL_CONTROL_RECORD_SIZE_BYTES,
    "proposed control-record CRC must occupy the final four bytes");

/* Codec and parser implementation intentionally deferred pending approval. */
