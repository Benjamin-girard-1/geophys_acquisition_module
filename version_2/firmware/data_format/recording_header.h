#ifndef GEOPHYS_RECORDING_HEADER_H
#define GEOPHYS_RECORDING_HEADER_H

#include <stddef.h>
#include <stdint.h>

/* A printable JSON descriptor occupies the first 4 KiB of each new file. */
#define RECORDING_HEADER_FORMAT_VERSION UINT8_C(2)
#define RECORDING_HEADER_STATIC_SIZE_BYTES UINT16_C(4096)
#define RECORDING_HEADER_TOTAL_SIZE_BYTES UINT32_C(32768)
#define RECORDING_HEADER_DATA_OFFSET_BYTES RECORDING_HEADER_TOTAL_SIZE_BYTES
#define RECORDING_HEADER_CHECKPOINT_OFFSET_BYTES \
    ((uint32_t)RECORDING_HEADER_STATIC_SIZE_BYTES)
#define RECORDING_HEADER_CHECKPOINT_SIZE_BYTES UINT16_C(32)
#define RECORDING_HEADER_CHECKPOINT_CAPACITY UINT16_C(896)
#define RECORDING_HEADER_CHECKPOINT_INTERVAL_MS UINT32_C(10000)
#define RECORDING_HEADER_TOOLS_URL \
    "https://github.com/Benjamin-girard-1/geophys_acquisition_module"

#define RECORDING_HEADER_RECORDING_NAME_SIZE_BYTES UINT8_C(32)
#define RECORDING_HEADER_DEVICE_ID_SIZE_BYTES UINT8_C(6)
#define RECORDING_HEADER_ADC_CHANNEL_COUNT UINT8_C(8)
#define RECORDING_HEADER_CARD_SLOT_COUNT UINT8_C(2)

#define RECORDING_HEADER_POWER_3V3_ENABLED UINT16_C(0x0001)
#define RECORDING_HEADER_POWER_5V_ENABLED UINT16_C(0x0002)
#define RECORDING_HEADER_POWER_9V_ENABLED UINT16_C(0x0004)
#define RECORDING_HEADER_POWER_NEGATIVE_5V_ENABLED UINT16_C(0x0008)
#define RECORDING_HEADER_POWER_18V_ENABLED UINT16_C(0x0010)

/** Logical input metadata; this compiler layout is never persisted. */
typedef struct {
    char recording_name[RECORDING_HEADER_RECORDING_NAME_SIZE_BYTES];
    uint8_t device_id[RECORDING_HEADER_DEVICE_ID_SIZE_BYTES];
    uint16_t hardware_version;
    uint16_t hardware_revision;
    uint32_t firmware_version;
    uint8_t protocol_version;
    uint64_t start_monotonic_timestamp_100ns;
    uint64_t start_unix_timestamp_us;
    uint32_t adc_sample_rate_sps;
    uint8_t adc_channel_mask;
    uint8_t adc_gains[RECORDING_HEADER_ADC_CHANNEL_COUNT];
    uint8_t card_types[RECORDING_HEADER_CARD_SLOT_COUNT];
    uint8_t gnss_state;
    uint8_t imu_state;
    uint16_t imu_averaging_time_ms;
    uint16_t power_rail_flags;
} recording_header_metadata_t;

typedef uint32_t (*recording_header_crc32_callback_t)(
    void *context,
    const uint8_t *data,
    size_t length_bytes);

typedef enum {
    RECORDING_HEADER_CODEC_OK = 0,
    RECORDING_HEADER_CODEC_INVALID_ARGUMENT,
    RECORDING_HEADER_CODEC_INVALID_FIELD,
} recording_header_codec_status_t;

/** Encode the first 4096 bytes as padded, CRC-protected ASCII JSON. */
recording_header_codec_status_t recording_header_encode_static(
    const recording_header_metadata_t *metadata,
    recording_header_crc32_callback_t crc32,
    void *crc_context,
    uint8_t header[RECORDING_HEADER_STATIC_SIZE_BYTES]);

#endif /* GEOPHYS_RECORDING_HEADER_H */
