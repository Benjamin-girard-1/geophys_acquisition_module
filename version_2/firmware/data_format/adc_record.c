#include "adc_record.h"

#include <string.h>

_Static_assert(
    ADC_RECORD_SAMPLE_PERIOD_OFFSET_BYTES + UINT16_C(4) ==
        ADC_RECORD_HEADER_SIZE_BYTES,
    "sample period must occupy the final four header bytes");

_Static_assert(
    ADC_RECORD_HEADER_SIZE_BYTES + ADC_RECORD_PAYLOAD_SIZE_BYTES +
        ADC_RECORD_CRC_SIZE_BYTES == ADC_RECORD_SIZE_BYTES,
    "ADC-record regions must total 512 bytes");

_Static_assert(
    ADC_RECORD_PAYLOAD_OFFSET_BYTES == ADC_RECORD_HEADER_SIZE_BYTES,
    "ADC-record payload must immediately follow its header");

_Static_assert(
    ADC_RECORD_CRC_OFFSET_BYTES + ADC_RECORD_CRC_SIZE_BYTES ==
        ADC_RECORD_SIZE_BYTES,
    "ADC-record CRC must occupy the final four bytes");

static void write_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void write_u64_le(uint8_t *destination, uint64_t value)
{
    for (uint8_t index = 0U; index < 8U; index++) {
        destination[index] = (uint8_t)(value >> (8U * index));
    }
}

static uint16_t read_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static uint64_t read_u64_le(const uint8_t *source)
{
    uint64_t value = 0U;
    for (uint8_t index = 0U; index < 8U; index++) {
        value |= (uint64_t)source[index] << (8U * index);
    }
    return value;
}

static bool status_is_valid(adc_record_status_t status)
{
    return status <= ADC_RECORD_STATUS_TIMING_ERROR;
}

static uint8_t conversion_count_for_mask(uint8_t channel_mask)
{
    if (channel_mask == UINT8_C(0x0F) ||
        channel_mask == UINT8_C(0xF0)) {
        return ADC_RECORD_FOUR_CHANNEL_CONVERSIONS;
    }
    if (channel_mask == UINT8_C(0xFF)) {
        return ADC_RECORD_EIGHT_CHANNEL_CONVERSIONS;
    }
    return 0U;
}

static bool metadata_is_valid(const adc_record_metadata_t *metadata)
{
    return metadata != NULL &&
           conversion_count_for_mask(metadata->channel_mask) != 0U &&
           status_is_valid(metadata->status) &&
           metadata->sample_period_100ns != 0U;
}

adc_record_codec_status_t adc_record_pack_sample(
    int32_t sample,
    uint8_t destination[ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES])
{
    if (destination == NULL) {
        return ADC_RECORD_CODEC_INVALID_ARGUMENT;
    }
    if (sample < -INT32_C(8388608) || sample > INT32_C(8388607)) {
        return ADC_RECORD_CODEC_INVALID_FIELD;
    }
    const uint32_t encoded = (uint32_t)sample & UINT32_C(0x00FFFFFF);
    destination[0] = (uint8_t)encoded;
    destination[1] = (uint8_t)(encoded >> 8U);
    destination[2] = (uint8_t)(encoded >> 16U);
    return ADC_RECORD_CODEC_OK;
}

int32_t adc_record_unpack_sample(
    const uint8_t source[ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES])
{
    if (source == NULL) {
        return 0;
    }
    const uint32_t encoded = (uint32_t)source[0] |
                             ((uint32_t)source[1] << 8U) |
                             ((uint32_t)source[2] << 16U);
    int32_t value = (int32_t)encoded;
    if ((encoded & UINT32_C(0x00800000)) != 0U) {
        value -= INT32_C(16777216);
    }
    return value;
}

adc_record_codec_status_t adc_record_builder_begin(
    adc_record_builder_t *builder,
    uint8_t record[ADC_RECORD_SIZE_BYTES],
    const adc_record_metadata_t *metadata)
{
    if (builder == NULL || record == NULL || metadata == NULL) {
        return ADC_RECORD_CODEC_INVALID_ARGUMENT;
    }
    memset(builder, 0, sizeof(*builder));
    if (!metadata_is_valid(metadata)) {
        return ADC_RECORD_CODEC_INVALID_FIELD;
    }

    memset(record, 0, ADC_RECORD_SIZE_BYTES);
    record[0] = ADC_RECORD_MAGIC_BYTE_0;
    record[1] = ADC_RECORD_MAGIC_BYTE_1;
    record[2] = ADC_RECORD_MAGIC_BYTE_2;
    record[3] = ADC_RECORD_MAGIC_BYTE_3;
    record[ADC_RECORD_CHANNEL_MASK_OFFSET_BYTES] = metadata->channel_mask;
    record[ADC_RECORD_STATUS_OFFSET_BYTES] = metadata->status;
    write_u16_le(record + ADC_RECORD_GAIN_OFFSET_BYTES,
                 metadata->packed_gain);
    write_u32_le(record + ADC_RECORD_PAYLOAD_NUMBER_OFFSET_BYTES,
                 metadata->payload_number);
    write_u32_le(record + ADC_RECORD_SAMPLE_PERIOD_OFFSET_BYTES,
                 metadata->sample_period_100ns);

    builder->record = record;
    builder->metadata = *metadata;
    builder->payload_offset = ADC_RECORD_PAYLOAD_OFFSET_BYTES;
    builder->required_conversions =
        conversion_count_for_mask(metadata->channel_mask);
    builder->begun = true;
    return ADC_RECORD_CODEC_OK;
}

adc_record_codec_status_t adc_record_builder_append(
    adc_record_builder_t *builder,
    uint32_t conversion_sequence,
    uint64_t monotonic_timestamp_100ns,
    const int32_t samples[ADC_RECORD_CHANNEL_COUNT])
{
    if (builder == NULL || samples == NULL) {
        return ADC_RECORD_CODEC_INVALID_ARGUMENT;
    }
    if (!builder->begun || builder->finalized || builder->record == NULL) {
        return ADC_RECORD_CODEC_INVALID_STATE;
    }
    if (builder->appended_conversions >= builder->required_conversions) {
        return ADC_RECORD_CODEC_INVALID_STATE;
    }

    if (builder->appended_conversions == 0U) {
        builder->metadata.first_conversion_sequence = conversion_sequence;
        builder->metadata.first_monotonic_timestamp_100ns =
            monotonic_timestamp_100ns;
        write_u32_le(builder->record + ADC_RECORD_SEQUENCE_OFFSET_BYTES,
                     conversion_sequence);
        write_u64_le(builder->record + ADC_RECORD_TIMESTAMP_OFFSET_BYTES,
                     monotonic_timestamp_100ns);
    }

    uint16_t payload_offset = builder->payload_offset;
    for (uint8_t channel = 0U;
         channel < ADC_RECORD_CHANNEL_COUNT;
         channel++) {
        if ((builder->metadata.channel_mask &
             (uint8_t)(UINT8_C(1) << channel)) == 0U) {
            continue;
        }
        if ((uint32_t)payload_offset + ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES >
            ADC_RECORD_CRC_OFFSET_BYTES) {
            return ADC_RECORD_CODEC_INVALID_STATE;
        }
        const adc_record_codec_status_t status = adc_record_pack_sample(
            samples[channel], builder->record + payload_offset);
        if (status != ADC_RECORD_CODEC_OK) {
            return status;
        }
        payload_offset = (uint16_t)(
            payload_offset + ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES);
    }

    builder->payload_offset = payload_offset;
    builder->appended_conversions++;
    return ADC_RECORD_CODEC_OK;
}

adc_record_codec_status_t adc_record_builder_set_status(
    adc_record_builder_t *builder,
    adc_record_status_t status)
{
    if (builder == NULL) {
        return ADC_RECORD_CODEC_INVALID_ARGUMENT;
    }
    if (!builder->begun || builder->finalized || builder->record == NULL) {
        return ADC_RECORD_CODEC_INVALID_STATE;
    }
    if (!status_is_valid(status)) {
        return ADC_RECORD_CODEC_INVALID_FIELD;
    }
    builder->metadata.status = status;
    builder->record[ADC_RECORD_STATUS_OFFSET_BYTES] = status;
    return ADC_RECORD_CODEC_OK;
}

adc_record_codec_status_t adc_record_builder_finalize(
    adc_record_builder_t *builder,
    adc_record_crc32_callback_t crc32,
    void *crc_context)
{
    if (builder == NULL || crc32 == NULL) {
        return ADC_RECORD_CODEC_INVALID_ARGUMENT;
    }
    if (!builder->begun || builder->finalized || builder->record == NULL) {
        return ADC_RECORD_CODEC_INVALID_STATE;
    }
    if (builder->appended_conversions != builder->required_conversions ||
        builder->payload_offset != ADC_RECORD_CRC_OFFSET_BYTES) {
        return ADC_RECORD_CODEC_INCOMPLETE;
    }
    const uint32_t checksum = crc32(
        crc_context, builder->record, ADC_RECORD_CRC_OFFSET_BYTES);
    write_u32_le(builder->record + ADC_RECORD_CRC_OFFSET_BYTES, checksum);
    builder->finalized = true;
    return ADC_RECORD_CODEC_OK;
}

adc_record_codec_status_t adc_record_validate(
    const uint8_t record[ADC_RECORD_SIZE_BYTES],
    adc_record_crc32_callback_t crc32,
    void *crc_context,
    adc_record_metadata_t *metadata)
{
    if (record == NULL || crc32 == NULL) {
        return ADC_RECORD_CODEC_INVALID_ARGUMENT;
    }
    if (record[0] != ADC_RECORD_MAGIC_BYTE_0 ||
        record[1] != ADC_RECORD_MAGIC_BYTE_1 ||
        record[2] != ADC_RECORD_MAGIC_BYTE_2 ||
        record[3] != ADC_RECORD_MAGIC_BYTE_3) {
        return ADC_RECORD_CODEC_INVALID_FIELD;
    }

    adc_record_metadata_t decoded = {
        .channel_mask = record[ADC_RECORD_CHANNEL_MASK_OFFSET_BYTES],
        .status = record[ADC_RECORD_STATUS_OFFSET_BYTES],
        .packed_gain = read_u16_le(record + ADC_RECORD_GAIN_OFFSET_BYTES),
        .payload_number = read_u32_le(
            record + ADC_RECORD_PAYLOAD_NUMBER_OFFSET_BYTES),
        .first_conversion_sequence = read_u32_le(
            record + ADC_RECORD_SEQUENCE_OFFSET_BYTES),
        .first_monotonic_timestamp_100ns = read_u64_le(
            record + ADC_RECORD_TIMESTAMP_OFFSET_BYTES),
        .sample_period_100ns = read_u32_le(
            record + ADC_RECORD_SAMPLE_PERIOD_OFFSET_BYTES),
    };
    if (!metadata_is_valid(&decoded)) {
        return ADC_RECORD_CODEC_INVALID_FIELD;
    }
    const uint32_t expected = read_u32_le(
        record + ADC_RECORD_CRC_OFFSET_BYTES);
    const uint32_t actual = crc32(
        crc_context, record, ADC_RECORD_CRC_OFFSET_BYTES);
    if (actual != expected) {
        return ADC_RECORD_CODEC_INTEGRITY;
    }
    if (metadata != NULL) {
        *metadata = decoded;
    }
    return ADC_RECORD_CODEC_OK;
}
