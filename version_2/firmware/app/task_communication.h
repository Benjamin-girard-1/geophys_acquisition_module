#ifndef GEOPHYS_TASK_COMMUNICATION_H
#define GEOPHYS_TASK_COMMUNICATION_H

#include <stdint.h>

#include "fw_error.h"
#include "protocol_messages.h"
#include "transport.h"

typedef struct {
    transport_interface_t transport;
    protocol_crc32_callback_t crc32;
    void *crc_context;
    protocol_device_info_t device_info;
    uint32_t read_timeout_us;
    uint32_t write_timeout_us;
} task_communication_config_t;

/** Create the sole command-transport owner and copy its startup configuration. */
fw_status_t task_communication_start(
    const task_communication_config_t *config,
    fw_error_context_t *error);

#endif /* GEOPHYS_TASK_COMMUNICATION_H */
