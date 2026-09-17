#ifndef GEOPHYS_PROTOCOL_MESSAGES_H
#define GEOPHYS_PROTOCOL_MESSAGES_H

#include <stdint.h>

#include "protocol_frame.h"

typedef uint16_t protocol_command_id_t;
typedef uint8_t protocol_command_result_t;

#define PROTOCOL_DIRECTION_TO_DEVICE         UINT8_C(0x00)
#define PROTOCOL_DIRECTION_TO_HOST           UINT8_C(0x01)

#define PROTOCOL_COMMAND_HELLO                UINT16_C(0x0001)
#define PROTOCOL_COMMAND_DEVICE_GET_CONFIG    UINT16_C(0x0002)
#define PROTOCOL_COMMAND_DEVICE_SET_CONFIG    UINT16_C(0x0003)
#define PROTOCOL_COMMAND_DEVICE_GET_DIAGNOSTIC UINT16_C(0x0004)
#define PROTOCOL_COMMAND_STREAMING_START      UINT16_C(0x0005)
#define PROTOCOL_COMMAND_STREAMING_STOP       UINT16_C(0x0006)
#define PROTOCOL_COMMAND_RECORDING_START      UINT16_C(0x0007)
#define PROTOCOL_COMMAND_RECORDING_STOP       UINT16_C(0x0008)
#define PROTOCOL_COMMAND_RECORDING_GET_NUMBER UINT16_C(0x0009)
#define PROTOCOL_COMMAND_RECORDING_GET_INFO   UINT16_C(0x000A)
#define PROTOCOL_COMMAND_RECORDING_DELETE     UINT16_C(0x000B)

#define PROTOCOL_REPLY_DEVICE_INFO            UINT16_C(0x00A1)
#define PROTOCOL_REPLY_DEVICE_CONFIG          UINT16_C(0x00A2)
#define PROTOCOL_REPLY_DEVICE_DIAGNOSTIC      UINT16_C(0x00A4)
#define PROTOCOL_REPLY_STREAMING_START_RESULT UINT16_C(0x00A5)
#define PROTOCOL_REPLY_STREAMING_STOP_RESULT  UINT16_C(0x00A6)
#define PROTOCOL_REPLY_RECORDING_START_RESULT UINT16_C(0x00A7)
#define PROTOCOL_REPLY_RECORDING_STOP_RESULT  UINT16_C(0x00A8)
#define PROTOCOL_REPLY_RECORDING_NUMBER       UINT16_C(0x00A9)
#define PROTOCOL_REPLY_RECORDING_INFO         UINT16_C(0x00AA)
#define PROTOCOL_REPLY_RECORDING_DELETE_RESULT UINT16_C(0x00AB)

#define PROTOCOL_RESULT_SUCCESS               UINT8_C(0x00)
#define PROTOCOL_RESULT_INVALID_ARGUMENT      UINT8_C(0x01)
#define PROTOCOL_RESULT_INVALID_STATE         UINT8_C(0x02)
#define PROTOCOL_RESULT_BUSY                  UINT8_C(0x03)
#define PROTOCOL_RESULT_UNSUPPORTED           UINT8_C(0x04)
#define PROTOCOL_RESULT_NOT_FOUND             UINT8_C(0x05)
#define PROTOCOL_RESULT_ALREADY_EXISTS        UINT8_C(0x06)
#define PROTOCOL_RESULT_NOT_READY             UINT8_C(0x07)
#define PROTOCOL_RESULT_TIMEOUT               UINT8_C(0x08)
#define PROTOCOL_RESULT_STORAGE_MEDIA_ABSENT  UINT8_C(0x09)
#define PROTOCOL_RESULT_STORAGE_FULL          UINT8_C(0x0A)
#define PROTOCOL_RESULT_IO_ERROR              UINT8_C(0x0B)
#define PROTOCOL_RESULT_INTEGRITY_ERROR       UINT8_C(0x0C)
#define PROTOCOL_RESULT_HARDWARE_FAULT        UINT8_C(0x0D)
#define PROTOCOL_RESULT_INTERNAL_ERROR        UINT8_C(0x0E)
#define PROTOCOL_RESULT_LIMIT_REACHED         UINT8_C(0x0F)

#define PROTOCOL_VERSION_CURRENT              UINT8_C(0x01)
#define PROTOCOL_DEVICE_INFO_PAYLOAD_SIZE_BYTES UINT8_C(16)
#define PROTOCOL_DEVICE_MAC_SIZE_BYTES        UINT8_C(6)
#define PROTOCOL_DEVICE_SET_CONFIG_PAYLOAD_SIZE_BYTES UINT8_C(39)
#define PROTOCOL_DEVICE_CONFIG_PAYLOAD_SIZE_BYTES UINT8_C(40)

#define PROTOCOL_ADC_SAMPLE_RATE_500_SPS       UINT8_C(0x00)
#define PROTOCOL_ADC_SAMPLE_RATE_1000_SPS      UINT8_C(0x01)
#define PROTOCOL_ADC_SAMPLE_RATE_2000_SPS      UINT8_C(0x02)
#define PROTOCOL_ADC_SAMPLE_RATE_4000_SPS      UINT8_C(0x03)
#define PROTOCOL_ADC_SAMPLE_RATE_8000_SPS      UINT8_C(0x04)
#define PROTOCOL_ADC_SAMPLE_RATE_16000_SPS     UINT8_C(0x05)

#define PROTOCOL_ADC_CHANNEL_MASK_NONE         UINT8_C(0x00)
#define PROTOCOL_ADC_CHANNEL_MASK_LOW          UINT8_C(0x0F)
#define PROTOCOL_ADC_CHANNEL_MASK_HIGH         UINT8_C(0xF0)
#define PROTOCOL_ADC_CHANNEL_MASK_ALL          UINT8_C(0xFF)

#define PROTOCOL_CARD_ABSENT                   UINT8_C(0x00)
#define PROTOCOL_CARD_MAGNETIC                 UINT8_C(0x01)
#define PROTOCOL_CARD_ACC_GEOPH                UINT8_C(0x02)

#define PROTOCOL_GNSS_DISABLED                 UINT8_C(0x00)
#define PROTOCOL_GNSS_READY                    UINT8_C(0x01)
#define PROTOCOL_GNSS_FAULTED                  UINT8_C(0x02)
#define PROTOCOL_GNSS_SEARCHING                UINT8_C(0x03)

#define PROTOCOL_IMU_DISABLED                  UINT8_C(0x00)
#define PROTOCOL_IMU_READY                     UINT8_C(0x01)
#define PROTOCOL_IMU_FAULTED                   UINT8_C(0x02)

#define PROTOCOL_SD_CARD_ABSENT                UINT8_C(0x00)
#define PROTOCOL_SD_CARD_PRESENT               UINT8_C(0x01)
#define PROTOCOL_SD_CARD_FAULTED               UINT8_C(0x02)

typedef enum {
    PROTOCOL_MESSAGE_OK = 0,
    PROTOCOL_MESSAGE_INVALID_ARGUMENT,
    PROTOCOL_MESSAGE_UNEXPECTED_ID,
    PROTOCOL_MESSAGE_UNEXPECTED_DIRECTION,
    PROTOCOL_MESSAGE_UNEXPECTED_LENGTH,
    PROTOCOL_MESSAGE_INVALID_RESULT,
    PROTOCOL_MESSAGE_INVALID_FIELD,
} protocol_message_status_t;

typedef struct {
    protocol_command_result_t result;
    uint8_t mac_address[PROTOCOL_DEVICE_MAC_SIZE_BYTES];
    uint16_t hardware_version;
    uint16_t hardware_revision;
    uint32_t firmware_version;
    uint8_t protocol_version;
} protocol_device_info_t;

/** Writable fields carried by DEVICE_SET_CONFIG in their wire representation. */
typedef struct {
    uint8_t adc_sample_rate;
    uint8_t adc_channel_mask;
    uint16_t adc_gain;
    uint8_t rail_3v3_enabled;
    uint8_t rail_5v_enabled;
    uint8_t rail_9v_enabled;
    uint8_t rail_negative_5v_enabled;
    uint8_t rail_18v_enabled;
    uint16_t imu_averaging_time_ms;
} protocol_device_config_update_t;

/** Complete DEVICE_CONFIG reply fields in their defined wire representation. */
typedef struct {
    protocol_command_result_t result;
    uint64_t timestamp_100ns;
    uint8_t recording_in_progress;
    uint8_t card_slot_1;
    uint8_t card_slot_2;
    uint8_t adc_sample_rate;
    uint8_t adc_channel_mask;
    uint16_t adc_gain;
    int16_t adc_temperature_centi_c;
    uint8_t rail_3v3_enabled;
    uint8_t rail_5v_enabled;
    uint8_t rail_9v_enabled;
    uint8_t rail_negative_5v_enabled;
    uint8_t rail_18v_enabled;
    uint8_t solar_present;
    uint8_t usb_5v_present;
    uint8_t gnss_state;
    uint8_t gnss_satellite_count;
    uint8_t imu_state;
    uint16_t imu_averaging_time_ms;
    int16_t imu_roll_centi_degrees;
    int16_t imu_pitch_centi_degrees;
    int16_t imu_temperature_centi_c;
    uint8_t sd_card_state;
    int16_t esp32_temperature_centi_c;
    uint8_t error_pending;
} protocol_device_config_t;

protocol_message_status_t protocol_decode_hello_request(
    const protocol_command_t *command);

protocol_message_status_t protocol_encode_device_info_reply(
    const protocol_device_info_t *device_info,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES]);

protocol_message_status_t protocol_decode_device_get_config_request(
    const protocol_command_t *command);

protocol_message_status_t protocol_decode_device_set_config_request(
    const protocol_command_t *command,
    protocol_device_config_update_t *update);

protocol_message_status_t protocol_encode_device_config_reply(
    const protocol_device_config_t *device_config,
    protocol_crc32_callback_t crc32,
    void *crc_context,
    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES]);

#endif /* GEOPHYS_PROTOCOL_MESSAGES_H */
