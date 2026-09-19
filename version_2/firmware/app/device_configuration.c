#include "device_configuration.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "platform_time.h"

#define DEVICE_CONFIGURATION_DEFAULT_SAMPLE_RATE_SPS UINT32_C(1000)
#define DEVICE_CONFIGURATION_DEFAULT_CHANNEL_MASK UINT8_C(0xFF)
#define DEVICE_CONFIGURATION_DEFAULT_GAIN UINT8_C(1)

typedef struct {
    device_configuration_snapshot_t snapshot;
    bool acquisition_active;
    bool initialized;
} device_configuration_state_t;

static device_configuration_state_t s_device_configuration;
static portMUX_TYPE s_device_configuration_lock = portMUX_INITIALIZER_UNLOCKED;

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
            .resource = FW_ERROR_RESOURCE_NONE,
            .operation = operation,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = detail,
        };
    }
    return status;
}

static bool sample_rate_is_valid(uint32_t sample_rate_sps)
{
    switch (sample_rate_sps) {
    case UINT32_C(500):
    case UINT32_C(1000):
    case UINT32_C(2000):
    case UINT32_C(4000):
    case UINT32_C(8000):
    case UINT32_C(16000):
        return true;
    default:
        return false;
    }
}

static bool channel_mask_is_valid(uint8_t channel_mask)
{
    return channel_mask == UINT8_C(0x00) ||
           channel_mask == UINT8_C(0x0F) ||
           channel_mask == UINT8_C(0xF0) ||
           channel_mask == UINT8_C(0xFF);
}

static bool gain_is_valid(uint8_t gain)
{
    return gain == UINT8_C(1) || gain == UINT8_C(2) ||
           gain == UINT8_C(4) || gain == UINT8_C(8);
}

fw_status_t device_configuration_initialize(fw_error_context_t *error)
{
    clear_error(error);
    if (s_device_configuration.initialized) {
        return FW_STATUS_OK;
    }

    memset(&s_device_configuration, 0, sizeof(s_device_configuration));
    s_device_configuration.snapshot.adc_sample_rate_sps =
        DEVICE_CONFIGURATION_DEFAULT_SAMPLE_RATE_SPS;
    s_device_configuration.snapshot.adc_channel_mask =
        DEVICE_CONFIGURATION_DEFAULT_CHANNEL_MASK;
    for (size_t channel = 0U;
         channel < DEVICE_CONFIGURATION_ADC_CHANNEL_COUNT;
         channel++) {
        s_device_configuration.snapshot.adc_gains[channel] =
            DEVICE_CONFIGURATION_DEFAULT_GAIN;
    }
    s_device_configuration.snapshot.gnss_state = DEVICE_GNSS_DISABLED;
    s_device_configuration.snapshot.imu_state = DEVICE_IMU_DISABLED;
    s_device_configuration.snapshot.sd_card_state = DEVICE_SD_CARD_ABSENT;
    s_device_configuration.initialized = true;
    return FW_STATUS_OK;
}

fw_status_t device_configuration_get(
    device_configuration_snapshot_t *snapshot,
    fw_error_context_t *error)
{
    clear_error(error);
    if (snapshot == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, 0U);
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_device_configuration.initialized) {
        return set_error(error, FW_STATUS_NOT_INITIALIZED,
                         FW_ERROR_OPERATION_READ, 0U);
    }

    portENTER_CRITICAL(&s_device_configuration_lock);
    *snapshot = s_device_configuration.snapshot;
    portEXIT_CRITICAL(&s_device_configuration_lock);
    return platform_monotonic_time_100ns(&snapshot->timestamp_100ns, error);
}

fw_status_t device_configuration_apply(
    const device_configuration_update_t *update,
    fw_error_context_t *error)
{
    clear_error(error);
    if (update == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_CONFIGURE, 0U);
    }
    if (!s_device_configuration.initialized) {
        return set_error(error, FW_STATUS_NOT_INITIALIZED,
                         FW_ERROR_OPERATION_CONFIGURE, 0U);
    }
    if (!sample_rate_is_valid(update->adc_sample_rate_sps) ||
        !channel_mask_is_valid(update->adc_channel_mask)) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_CONFIGURE,
                         update->adc_sample_rate_sps);
    }
    for (size_t channel = 0U;
         channel < DEVICE_CONFIGURATION_ADC_CHANNEL_COUNT;
         channel++) {
        if (!gain_is_valid(update->adc_gains[channel])) {
            return set_error(
                error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_CONFIGURE,
                ((uint32_t)channel << 16U) | update->adc_gains[channel]);
        }
    }
    portENTER_CRITICAL(&s_device_configuration_lock);
    if (s_device_configuration.snapshot.recording_in_progress ||
        s_device_configuration.acquisition_active) {
        portEXIT_CRITICAL(&s_device_configuration_lock);
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_CONFIGURE, 0U);
    }

    /*
     * These fields are wire-writable, but their safe runtime owners are not
     * implemented. Reject the complete update if any one would change so the
     * ADC subset is never partially committed.
     */
    if (update->rail_3v3_enabled !=
            s_device_configuration.snapshot.rail_3v3_enabled ||
        update->rail_5v_enabled !=
            s_device_configuration.snapshot.rail_5v_enabled ||
        update->rail_9v_enabled !=
            s_device_configuration.snapshot.rail_9v_enabled ||
        update->rail_negative_5v_enabled !=
            s_device_configuration.snapshot.rail_negative_5v_enabled ||
        update->rail_18v_enabled !=
            s_device_configuration.snapshot.rail_18v_enabled ||
        update->imu_averaging_time_ms !=
            s_device_configuration.snapshot.imu_averaging_time_ms) {
        portEXIT_CRITICAL(&s_device_configuration_lock);
        return set_error(error, FW_STATUS_UNSUPPORTED,
                         FW_ERROR_OPERATION_CONFIGURE, 0U);
    }

    s_device_configuration.snapshot.adc_sample_rate_sps =
        update->adc_sample_rate_sps;
    s_device_configuration.snapshot.adc_channel_mask =
        update->adc_channel_mask;
    memcpy(s_device_configuration.snapshot.adc_gains,
           update->adc_gains,
           sizeof(s_device_configuration.snapshot.adc_gains));
    portEXIT_CRITICAL(&s_device_configuration_lock);
    return FW_STATUS_OK;
}

void device_configuration_set_storage_state(device_sd_card_state_t state)
{
    if (!s_device_configuration.initialized ||
        state > DEVICE_SD_CARD_FAULTED) {
        return;
    }
    portENTER_CRITICAL(&s_device_configuration_lock);
    s_device_configuration.snapshot.sd_card_state = state;
    portEXIT_CRITICAL(&s_device_configuration_lock);
}

void device_configuration_set_acquisition_state(bool active,
                                                bool recording)
{
    if (!s_device_configuration.initialized) {
        return;
    }
    portENTER_CRITICAL(&s_device_configuration_lock);
    s_device_configuration.acquisition_active = active;
    s_device_configuration.snapshot.recording_in_progress = recording;
    s_device_configuration.snapshot.rail_3v3_enabled = active;
    s_device_configuration.snapshot.rail_9v_enabled = active;
    s_device_configuration.snapshot.rail_negative_5v_enabled = active;
    s_device_configuration.snapshot.rail_18v_enabled = false;
    portEXIT_CRITICAL(&s_device_configuration_lock);
}
