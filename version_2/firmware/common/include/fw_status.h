#ifndef GEOPHYS_FW_STATUS_H
#define GEOPHYS_FW_STATUS_H

/**
 * @brief Portable result categories shared by firmware modules.
 *
 * These values are internal firmware categories. They must be mapped
 * explicitly to stable V2 protocol error codes rather than serialized
 * directly.
 */
typedef enum {
    FW_STATUS_OK = 0,
    FW_STATUS_INVALID_ARGUMENT,
    FW_STATUS_INVALID_STATE,
    FW_STATUS_NOT_INITIALIZED,
    FW_STATUS_NOT_FOUND,
    FW_STATUS_BUSY,
    FW_STATUS_TIMEOUT,
    FW_STATUS_IO,
    FW_STATUS_INTEGRITY,
    FW_STATUS_OVERFLOW,
    FW_STATUS_UNSUPPORTED,
    FW_STATUS_HARDWARE_FAULT,
    FW_STATUS_INTERNAL,
} fw_status_t;

#endif /* GEOPHYS_FW_STATUS_H */
