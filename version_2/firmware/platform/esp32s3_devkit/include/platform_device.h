#ifndef GEOPHYS_PLATFORM_DEVICE_H
#define GEOPHYS_PLATFORM_DEVICE_H

#include <stdint.h>

#include "fw_error.h"

#define PLATFORM_DEVICE_MAC_SIZE_BYTES UINT8_C(6)

/** Read the factory-programmed base MAC address in canonical octet order. */
fw_status_t platform_device_read_base_mac(
    uint8_t mac_address[PLATFORM_DEVICE_MAC_SIZE_BYTES],
    fw_error_context_t *error);

#endif /* GEOPHYS_PLATFORM_DEVICE_H */
