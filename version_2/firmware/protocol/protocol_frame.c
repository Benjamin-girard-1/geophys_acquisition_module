#include "protocol_frame.h"

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

/* Codec and parser implementation intentionally deferred pending test vectors. */
