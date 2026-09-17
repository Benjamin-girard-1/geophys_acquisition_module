#include "platform_device.h"

#include "esp_mac.h"
#include "platform_error.h"

fw_status_t platform_device_read_base_mac(
    uint8_t mac_address[PLATFORM_DEVICE_MAC_SIZE_BYTES],
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (mac_address == NULL) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_NONE,
            FW_ERROR_OPERATION_READ, FW_ERROR_INSTANCE_NONE, 0U);
    }

    return platform_error_from_esp_err(
        esp_efuse_mac_get_default(mac_address), error,
        FW_ERROR_RESOURCE_NONE, FW_ERROR_OPERATION_READ,
        FW_ERROR_INSTANCE_NONE, PLATFORM_DEVICE_MAC_SIZE_BYTES);
}
