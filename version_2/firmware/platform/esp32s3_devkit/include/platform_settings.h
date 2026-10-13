#ifndef GEOPHYS_PLATFORM_SETTINGS_H
#define GEOPHYS_PLATFORM_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "fw_error.h"

/** Initialize the ESP32 nonvolatile key-value store. This call is idempotent. */
fw_status_t platform_settings_initialize(fw_error_context_t *error);

/**
 * Read one opaque blob from internal flash.
 *
 * namespace_name and key must satisfy the ESP32 NVS naming limits. The actual
 * blob size is returned in length_bytes. FW_STATUS_NOT_FOUND means no value has
 * been saved. FW_STATUS_OVERFLOW means the stored blob is larger than capacity.
 */
fw_status_t platform_settings_read_blob(
    const char *namespace_name,
    const char *key,
    uint8_t *data,
    size_t capacity_bytes,
    size_t *length_bytes,
    fw_error_context_t *error);

/** Atomically commit one opaque blob to internal flash. */
fw_status_t platform_settings_write_blob(
    const char *namespace_name,
    const char *key,
    const uint8_t *data,
    size_t length_bytes,
    fw_error_context_t *error);

#endif /* GEOPHYS_PLATFORM_SETTINGS_H */
