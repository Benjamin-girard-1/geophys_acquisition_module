#ifndef GEOPHYS_PROTOCOL_MESSAGES_H
#define GEOPHYS_PROTOCOL_MESSAGES_H

#include <stdint.h>

/*
 * Scaffold types for stable serialized values. Numeric message identifiers,
 * status codes, payload layouts, and encode/decode APIs remain deliberately
 * undefined until shared/protocol/protocol_types.md is approved.
 */
typedef uint8_t protocol_message_type_t;
typedef uint16_t protocol_status_code_t;

#endif /* GEOPHYS_PROTOCOL_MESSAGES_H */
