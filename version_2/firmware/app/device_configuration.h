#ifndef GEOPHYS_DEVICE_CONFIGURATION_H
#define GEOPHYS_DEVICE_CONFIGURATION_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_error.h"
#include "fw_time.h"

#define DEVICE_CONFIGURATION_ADC_CHANNEL_COUNT UINT8_C(8)

typedef enum {
    DEVICE_CARD_ABSENT = 0,
    DEVICE_CARD_MAGNETIC,
    DEVICE_CARD_ACC_GEOPH,
} device_card_type_t;

typedef enum {
    DEVICE_GNSS_DISABLED = 0,
    DEVICE_GNSS_READY,
    DEVICE_GNSS_FAULTED,
    DEVICE_GNSS_SEARCHING,
} device_gnss_state_t;

typedef enum {
    DEVICE_IMU_DISABLED = 0,
    DEVICE_IMU_READY,
    DEVICE_IMU_FAULTED,
} device_imu_state_t;

typedef enum {
    DEVICE_SD_CARD_ABSENT = 0,
    DEVICE_SD_CARD_PRESENT,
    DEVICE_SD_CARD_FAULTED,
} device_sd_card_state_t;

/** Product-level configuration/status snapshot; this is not a wire layout. */
typedef struct {
    fw_monotonic_100ns_t timestamp_100ns;
    bool recording_in_progress;
    device_card_type_t card_slots[2];
    uint32_t adc_sample_rate_sps;
    uint8_t adc_channel_mask;
    uint8_t adc_gains[DEVICE_CONFIGURATION_ADC_CHANNEL_COUNT];
    int16_t adc_temperature_centi_c;
    bool rail_3v3_enabled;
    bool rail_5v_enabled;
    bool rail_9v_enabled;
    bool rail_negative_5v_enabled;
    bool rail_18v_enabled;
    bool solar_present;
    bool usb_5v_present;
    device_gnss_state_t gnss_state;
    uint8_t gnss_satellite_count;
    device_imu_state_t imu_state;
    uint16_t imu_averaging_time_ms;
    int16_t imu_roll_centi_degrees;
    int16_t imu_pitch_centi_degrees;
    int16_t imu_temperature_centi_c;
    device_sd_card_state_t sd_card_state;
    int16_t esp32_temperature_centi_c;
    bool error_pending;
} device_configuration_snapshot_t;

/** Writable product-level fields requested by DEVICE_SET_CONFIG. */
typedef struct {
    uint32_t adc_sample_rate_sps;
    uint8_t adc_channel_mask;
    uint8_t adc_gains[DEVICE_CONFIGURATION_ADC_CHANNEL_COUNT];
    bool rail_3v3_enabled;
    bool rail_5v_enabled;
    bool rail_9v_enabled;
    bool rail_negative_5v_enabled;
    bool rail_18v_enabled;
    uint16_t imu_averaging_time_ms;
} device_configuration_update_t;

/** Initialize the stopped-device defaults before communication starts. */
fw_status_t device_configuration_initialize(fw_error_context_t *error);

/** Copy a coherent status/configuration snapshot and stamp it when read. */
fw_status_t device_configuration_get(
    device_configuration_snapshot_t *snapshot,
    fw_error_context_t *error);

/**
 * Validate and atomically apply currently supported writable fields.
 *
 * ADC rate, mask, and gains are accepted while stopped. Rail and IMU changes
 * remain unsupported until their runtime owners and sequencing are present.
 */
fw_status_t device_configuration_apply(
    const device_configuration_update_t *update,
    fw_error_context_t *error);

#endif /* GEOPHYS_DEVICE_CONFIGURATION_H */
