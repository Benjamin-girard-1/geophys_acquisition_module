#ifndef GEOPHYS_ADC_RECORD_H
#define GEOPHYS_ADC_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Portable 512-byte data block defined in shared/protocol/protocol.md. */
#define ADC_RECORD_SIZE_BYTES                 UINT16_C(512)
#define ADC_RECORD_HEADER_SIZE_BYTES          UINT16_C(28)
#define ADC_RECORD_PAYLOAD_SIZE_BYTES         UINT16_C(480)
#define ADC_RECORD_CRC_SIZE_BYTES             UINT16_C(4)
#define ADC_RECORD_MAGIC_OFFSET_BYTES         UINT16_C(0)
#define ADC_RECORD_MAGIC_SIZE_BYTES           UINT16_C(4)
#define ADC_RECORD_MAGIC_BYTE_0               UINT8_C(0x5C)
#define ADC_RECORD_MAGIC_BYTE_1               UINT8_C(0x44)
#define ADC_RECORD_MAGIC_BYTE_2               UINT8_C(0x41)
#define ADC_RECORD_MAGIC_BYTE_3               UINT8_C(0x54)
#define ADC_RECORD_CHANNEL_MASK_OFFSET_BYTES  UINT16_C(4)
#define ADC_RECORD_STATUS_OFFSET_BYTES        UINT16_C(5)
#define ADC_RECORD_GAIN_OFFSET_BYTES          UINT16_C(6)
#define ADC_RECORD_PAYLOAD_NUMBER_OFFSET_BYTES UINT16_C(8)
#define ADC_RECORD_SEQUENCE_OFFSET_BYTES      UINT16_C(12)
#define ADC_RECORD_TIMESTAMP_OFFSET_BYTES     UINT16_C(16)
#define ADC_RECORD_SAMPLE_PERIOD_OFFSET_BYTES UINT16_C(24)
#define ADC_RECORD_PAYLOAD_OFFSET_BYTES       UINT16_C(28)
#define ADC_RECORD_CRC_OFFSET_BYTES           UINT16_C(508)
#define ADC_RECORD_CHANNEL_COUNT              UINT8_C(8)
#define ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES   UINT8_C(3)
#define ADC_RECORD_FOUR_CHANNEL_CONVERSIONS   UINT8_C(40)
#define ADC_RECORD_EIGHT_CHANNEL_CONVERSIONS  UINT8_C(20)

typedef uint8_t adc_record_status_t;

#define ADC_RECORD_STATUS_OK                  UINT8_C(0x00)
#define ADC_RECORD_STATUS_CRITICAL_ERROR      UINT8_C(0x01)
#define ADC_RECORD_STATUS_CONVERSION_ERROR    UINT8_C(0x02)
#define ADC_RECORD_STATUS_TIMING_ERROR        UINT8_C(0x03)

/**
 * @brief Logical metadata for one ADC data block.
 *
 * This type is convenient input/output state only. Its compiler layout is not
 * the persistent or wire representation and must never be copied into a record.
 */
typedef struct {
    uint8_t channel_mask;
    adc_record_status_t status;
    uint16_t packed_gain;
    uint32_t payload_number;
    uint32_t first_conversion_sequence;
    uint64_t first_monotonic_timestamp_100ns;
    uint32_t sample_period_100ns;
} adc_record_metadata_t;

typedef uint32_t (*adc_record_crc32_callback_t)(
    void *context,
    const uint8_t *data,
    size_t length_bytes);

typedef enum {
    ADC_RECORD_CODEC_OK = 0,
    ADC_RECORD_CODEC_INVALID_ARGUMENT,
    ADC_RECORD_CODEC_INVALID_STATE,
    ADC_RECORD_CODEC_INVALID_FIELD,
    ADC_RECORD_CODEC_INCOMPLETE,
    ADC_RECORD_CODEC_INTEGRITY,
} adc_record_codec_status_t;

/** Stateful, allocation-free encoder for exactly one complete record. */
typedef struct {
    uint8_t *record;
    adc_record_metadata_t metadata;
    uint16_t payload_offset;
    uint8_t required_conversions;
    uint8_t appended_conversions;
    bool begun;
    bool finalized;
} adc_record_builder_t;

/** Begin a record. Only masks 0x0F, 0xF0, and 0xFF are recordable. */
adc_record_codec_status_t adc_record_builder_begin(
    adc_record_builder_t *builder,
    uint8_t record[ADC_RECORD_SIZE_BYTES],
    const adc_record_metadata_t *metadata);

/** Append one simultaneous conversion in ascending selected-channel order. */
adc_record_codec_status_t adc_record_builder_append(
    adc_record_builder_t *builder,
    uint32_t conversion_sequence,
    uint64_t monotonic_timestamp_100ns,
    const int32_t samples[ADC_RECORD_CHANNEL_COUNT]);

/** Replace the block status before finalization. */
adc_record_codec_status_t adc_record_builder_set_status(
    adc_record_builder_t *builder,
    adc_record_status_t status);

/** Finalize only when all 480 sample bytes are populated. */
adc_record_codec_status_t adc_record_builder_finalize(
    adc_record_builder_t *builder,
    adc_record_crc32_callback_t crc32,
    void *crc_context);

/** Validate a complete record and optionally decode its logical metadata. */
adc_record_codec_status_t adc_record_validate(
    const uint8_t record[ADC_RECORD_SIZE_BYTES],
    adc_record_crc32_callback_t crc32,
    void *crc_context,
    adc_record_metadata_t *metadata);

/** Encode/decode one signed 24-bit little-endian two's-complement sample. */
adc_record_codec_status_t adc_record_pack_sample(
    int32_t sample,
    uint8_t destination[ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES]);
int32_t adc_record_unpack_sample(
    const uint8_t source[ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES]);

#endif /* GEOPHYS_ADC_RECORD_H */
