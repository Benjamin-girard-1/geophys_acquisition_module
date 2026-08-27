#ifndef GEOPHYS_TRANSPORT_H
#define GEOPHYS_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "fw_error.h"

/**
 * @brief Read currently available transport bytes after waiting for the first.
 *
 * A successful operation may return fewer bytes than capacity_bytes. A timeout
 * may also report bytes already consumed before the deadline was observed, so
 * callers must always inspect bytes_read. error is optional.
 */
typedef fw_status_t (*transport_read_some_callback_t)(
    void *context,
    uint8_t *data,
    size_t capacity_bytes,
    size_t *bytes_read,
    uint32_t timeout_us,
    fw_error_context_t *error);

/**
 * @brief Submit as many transport bytes as currently fit.
 *
 * A successful operation may accept fewer bytes than length_bytes. The caller
 * retains ownership of unaccepted bytes and must continue from bytes_written.
 * error is optional.
 */
typedef fw_status_t (*transport_write_some_callback_t)(
    void *context,
    const uint8_t *data,
    size_t length_bytes,
    size_t *bytes_written,
    uint32_t timeout_us,
    fw_error_context_t *error);

typedef struct {
    transport_read_some_callback_t read_some;
    transport_write_some_callback_t write_some;
    void *context;
} transport_interface_t;

#endif /* GEOPHYS_TRANSPORT_H */
