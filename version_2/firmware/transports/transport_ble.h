#ifndef GEOPHYS_TRANSPORT_BLE_H
#define GEOPHYS_TRANSPORT_BLE_H

#include <stddef.h>
#include <stdint.h>

#include "fw_error.h"

#define TRANSPORT_BLE_DEVICE_NAME "Geophys Acquisition"
#define TRANSPORT_BLE_SERVICE_UUID_STRING \
    "3809d383-3dc8-4280-b537-2bfeab1b8acb"
#define TRANSPORT_BLE_RX_UUID_STRING \
    "181481bc-cb34-43be-864f-54d9453afe22"
#define TRANSPORT_BLE_TX_UUID_STRING \
    "7201ba3a-7799-4409-9f59-bd3711612354"

typedef void (*transport_ble_receive_callback_t)(
    void *context,
    const uint8_t *data,
    size_t length_bytes);

typedef struct {
    transport_ble_receive_callback_t receive;
    void *receive_context;
} transport_ble_config_t;

/**
 * @brief Start the BLE peripheral, GATT service, and continuous advertising.
 *
 * Advertising is independent of the UART-to-USB session. One BLE central may
 * connect at a time. Incoming characteristic writes are delivered as byte
 * fragments and are not interpreted by this transport.
 */
fw_status_t transport_ble_start(const transport_ble_config_t *config,
                                fw_error_context_t *error);

/**
 * @brief Notify the connected and subscribed central with ordered bytes.
 *
 * The transport fragments the supplied bytes to the negotiated ATT payload
 * size. The caller retains ownership of data.
 */
fw_status_t transport_ble_notify(const uint8_t *data,
                                 size_t length_bytes,
                                 fw_error_context_t *error);

#endif /* GEOPHYS_TRANSPORT_BLE_H */
