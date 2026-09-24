#include "transport_ble.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TRANSPORT_BLE_WRITE_MAX_BYTES UINT16_C(512)

typedef struct {
    transport_ble_config_t config;
    uint16_t connection_handle;
    uint16_t tx_value_handle;
    uint8_t own_address_type;
    bool connected;
    bool tx_subscribed;
    bool started;
} transport_ble_state_t;

static transport_ble_state_t s_ble;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0xcb, 0x8a, 0x1b, 0xab, 0xfe, 0x2b, 0x37, 0xb5,
    0x80, 0x42, 0xc8, 0x3d, 0x83, 0xd3, 0x09, 0x38);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x22, 0xfe, 0x3a, 0x45, 0xd9, 0x54, 0x4f, 0x86,
    0xbe, 0x43, 0x34, 0xcb, 0xbc, 0x81, 0x14, 0x18);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x54, 0x23, 0x61, 0x11, 0x37, 0xbd, 0x59, 0x9f,
    0x09, 0x44, 0x99, 0x77, 0x3a, 0xba, 0x01, 0x72);

static void clear_error(fw_error_context_t *error)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = FW_STATUS_OK,
            .resource = FW_ERROR_RESOURCE_NONE,
            .operation = FW_ERROR_OPERATION_NONE,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = 0U,
        };
    }
}

static fw_status_t set_error(fw_error_context_t *error,
                             fw_status_t status,
                             fw_error_operation_t operation,
                             uint32_t detail)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_BLUETOOTH,
            .operation = operation,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = detail,
        };
    }
    return status;
}

static fw_status_t status_from_ble_error(int rc)
{
    switch (rc) {
    case 0:
        return FW_STATUS_OK;
    case BLE_HS_ENOMEM:
    case BLE_HS_EBUSY:
        return FW_STATUS_BUSY;
    case BLE_HS_ENOTCONN:
        return FW_STATUS_NOT_INITIALIZED;
    case BLE_HS_ETIMEOUT:
        return FW_STATUS_TIMEOUT;
    case BLE_HS_EINVAL:
        return FW_STATUS_INVALID_ARGUMENT;
    default:
        return FW_STATUS_IO;
    }
}

static void advertise(void);

static int gap_event(struct ble_gap_event *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_ble.connection_handle = event->connect.conn_handle;
            s_ble.connected = true;
            s_ble.tx_subscribed = false;
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_ble.connection_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ble.connected = false;
        s_ble.tx_subscribed = false;
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_ble.tx_value_handle) {
            s_ble.tx_subscribed = event->subscribe.cur_notify != 0U;
        }
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) {
        return;
    }

    struct ble_hs_adv_fields response_fields;
    memset(&response_fields, 0, sizeof(response_fields));
    const char *name = ble_svc_gap_device_name();
    response_fields.name = (uint8_t *)name;
    response_fields.name_len = strlen(name);
    response_fields.name_is_complete = 1;
    if (ble_gap_adv_rsp_set_fields(&response_fields) != 0) {
        return;
    }

    struct ble_gap_adv_params parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    (void)ble_gap_adv_start(s_ble.own_address_type, NULL, BLE_HS_FOREVER,
                            &parameters, gap_event, NULL);
}

static void on_reset(int reason)
{
    (void)reason;
    s_ble.connection_handle = BLE_HS_CONN_HANDLE_NONE;
    s_ble.connected = false;
    s_ble.tx_subscribed = false;
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &s_ble.own_address_type) != 0) {
        return;
    }
    advertise();
}

static int receive_access(uint16_t connection_handle,
                          uint16_t attribute_handle,
                          struct ble_gatt_access_ctxt *context,
                          void *argument)
{
    (void)connection_handle;
    (void)attribute_handle;
    (void)argument;
    if (context == NULL || context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t length_bytes = OS_MBUF_PKTLEN(context->om);
    if (length_bytes == 0U ||
        length_bytes > TRANSPORT_BLE_WRITE_MAX_BYTES) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t data[TRANSPORT_BLE_WRITE_MAX_BYTES];
    if (os_mbuf_copydata(context->om, 0, length_bytes, data) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    s_ble.config.receive(
        s_ble.config.receive_context, data, (size_t)length_bytes);
    return 0;
}

static int transmit_access(uint16_t connection_handle,
                           uint16_t attribute_handle,
                           struct ble_gatt_access_ctxt *context,
                           void *argument)
{
    (void)connection_handle;
    (void)attribute_handle;
    (void)context;
    (void)argument;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = receive_access,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = transmit_access,
                .val_handle = &s_ble.tx_value_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

static void host_task(void *context)
{
    (void)context;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

fw_status_t transport_ble_start(const transport_ble_config_t *config,
                                fw_error_context_t *error)
{
    clear_error(error);
    if (config == NULL || config->receive == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    if (s_ble.started) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }

    esp_err_t nvs_status = nvs_flash_init();
    if (nvs_status == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_status = nvs_flash_erase();
        if (nvs_status == ESP_OK) {
            nvs_status = nvs_flash_init();
        }
    }
    if (nvs_status != ESP_OK) {
        return set_error(error, FW_STATUS_IO,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }

    if (nimble_port_init() != ESP_OK) {
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }

    memset(&s_ble, 0, sizeof(s_ble));
    s_ble.config = *config;
    s_ble.connection_handle = BLE_HS_CONN_HANDLE_NONE;

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(s_services) != 0 ||
        ble_gatts_add_svcs(s_services) != 0 ||
        ble_svc_gap_device_name_set(TRANSPORT_BLE_DEVICE_NAME) != 0) {
        (void)nimble_port_deinit();
        memset(&s_ble, 0, sizeof(s_ble));
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_CONFIGURE, 0U);
    }

    s_ble.started = true;
    nimble_port_freertos_init(host_task);
    return FW_STATUS_OK;
}

fw_status_t transport_ble_notify(const uint8_t *data,
                                 size_t length_bytes,
                                 fw_error_context_t *error)
{
    clear_error(error);
    if (data == NULL || length_bytes == 0U) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }
    if (!s_ble.started || !s_ble.connected || !s_ble.tx_subscribed) {
        return set_error(error, FW_STATUS_NOT_INITIALIZED,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }

    const uint16_t mtu = ble_att_mtu(s_ble.connection_handle);
    const size_t payload_limit = mtu > 3U ? (size_t)(mtu - 3U) : 0U;
    if (payload_limit == 0U) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }

    size_t offset = 0U;
    while (offset < length_bytes) {
        size_t chunk_length = length_bytes - offset;
        if (chunk_length > payload_limit) {
            chunk_length = payload_limit;
        }
        struct os_mbuf *packet = ble_hs_mbuf_from_flat(
            data + offset, (uint16_t)chunk_length);
        if (packet == NULL) {
            return set_error(error, FW_STATUS_BUSY,
                             FW_ERROR_OPERATION_WRITE,
                             (uint32_t)offset);
        }
        const int rc = ble_gatts_notify_custom(
            s_ble.connection_handle, s_ble.tx_value_handle, packet);
        if (rc != 0) {
            return set_error(error, status_from_ble_error(rc),
                             FW_ERROR_OPERATION_WRITE,
                             (uint32_t)offset);
        }
        offset += chunk_length;
    }
    return FW_STATUS_OK;
}
