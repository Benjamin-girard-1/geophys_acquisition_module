#ifndef GEOPHYS_TASK_BLUETOOTH_H
#define GEOPHYS_TASK_BLUETOOTH_H

#include "fw_error.h"
#include "protocol_messages.h"

typedef struct {
    protocol_crc32_callback_t crc32;
    void *crc_context;
    protocol_device_info_t device_info;
} task_bluetooth_config_t;

/** Start BLE advertising and the HELLO-only Bluetooth command slice. */
fw_status_t task_bluetooth_start(const task_bluetooth_config_t *config,
                                 fw_error_context_t *error);

#endif /* GEOPHYS_TASK_BLUETOOTH_H */
