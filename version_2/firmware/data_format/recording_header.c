#include "recording_header.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

_Static_assert(
    RECORDING_HEADER_CHECKPOINT_OFFSET_BYTES +
        ((uint32_t)RECORDING_HEADER_CHECKPOINT_SIZE_BYTES *
         RECORDING_HEADER_CHECKPOINT_CAPACITY) ==
        RECORDING_HEADER_TOTAL_SIZE_BYTES,
    "checkpoint array must end where ADC data begins");

static uint8_t channel_count(uint8_t mask)
{
    uint8_t count = 0U;
    for (uint8_t channel = 0U;
         channel < RECORDING_HEADER_ADC_CHANNEL_COUNT; channel++) {
        count = (uint8_t)(count + ((mask >> channel) & UINT8_C(1)));
    }
    return count;
}

static bool gain_code(uint8_t gain, uint16_t *code)
{
    switch (gain) {
    case 1U: *code = 0U; return true;
    case 2U: *code = 1U; return true;
    case 4U: *code = 2U; return true;
    case 8U: *code = 3U; return true;
    default: return false;
    }
}

static bool name_is_valid(
    const char name[RECORDING_HEADER_RECORDING_NAME_SIZE_BYTES])
{
    if (name[0] == '\0') {
        return false;
    }
    bool terminated = false;
    for (size_t index = 0U;
         index < RECORDING_HEADER_RECORDING_NAME_SIZE_BYTES; index++) {
        const uint8_t value = (uint8_t)name[index];
        if (terminated) {
            if (value != 0U) {
                return false;
            }
        } else if (value == 0U) {
            terminated = true;
        } else if (!((value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
                     (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
                     value == (uint8_t)'_' || value == (uint8_t)'-')) {
            return false;
        }
    }
    return terminated;
}

static bool metadata_is_valid(const recording_header_metadata_t *metadata,
                              uint16_t *packed_gain)
{
    if (!name_is_valid(metadata->recording_name) ||
        (metadata->adc_channel_mask != UINT8_C(0x0F) &&
         metadata->adc_channel_mask != UINT8_C(0xF0) &&
         metadata->adc_channel_mask != UINT8_C(0xFF)) ||
        metadata->adc_sample_rate_sps == 0U ||
        (UINT32_C(10000000) % metadata->adc_sample_rate_sps) != 0U) {
        return false;
    }
    uint16_t packed = 0U;
    for (uint8_t channel = 0U;
         channel < RECORDING_HEADER_ADC_CHANNEL_COUNT; channel++) {
        uint16_t code = 0U;
        if (!gain_code(metadata->adc_gains[channel], &code)) {
            return false;
        }
        packed |= (uint16_t)(code << (2U * channel));
    }
    *packed_gain = packed;
    return true;
}

recording_header_codec_status_t recording_header_encode_static(
    const recording_header_metadata_t *metadata,
    recording_header_crc32_callback_t crc32,
    void *crc_context,
    uint8_t header[RECORDING_HEADER_STATIC_SIZE_BYTES])
{
    if (metadata == NULL || crc32 == NULL || header == NULL) {
        return RECORDING_HEADER_CODEC_INVALID_ARGUMENT;
    }
    uint16_t packed_gain = 0U;
    if (!metadata_is_valid(metadata, &packed_gain)) {
        return RECORDING_HEADER_CODEC_INVALID_FIELD;
    }

    memset(header, ' ', RECORDING_HEADER_STATIC_SIZE_BYTES);
    const int used = snprintf(
        (char *)header, RECORDING_HEADER_STATIC_SIZE_BYTES,
        "{\n"
        "  \"format\": \"GEO_RECORDING\",\n"
        "  \"format_version\": %u,\n"
        "  \"tools_url\": \"%s\",\n"
        "  \"recording_name\": \"%s\",\n"
        "  \"device_id\": \"%02X:%02X:%02X:%02X:%02X:%02X\",\n"
        "  \"hardware_version\": %u,\n"
        "  \"hardware_revision\": %u,\n"
        "  \"firmware_version\": %" PRIu32 ",\n"
        "  \"protocol_version\": %u,\n"
        "  \"start_monotonic_timestamp_100ns\": %" PRIu64 ",\n"
        "  \"start_unix_timestamp_us\": %" PRIu64 ",\n"
        "  \"adc_sample_rate_sps\": %" PRIu32 ",\n"
        "  \"adc_sample_period_100ns\": %" PRIu32 ",\n"
        "  \"adc_channel_mask\": %u,\n"
        "  \"adc_channel_count\": %u,\n"
        "  \"adc_gains\": [%u, %u, %u, %u, %u, %u, %u, %u],\n"
        "  \"packed_gain\": %u,\n"
        "  \"card_types\": [%u, %u],\n"
        "  \"gnss_state\": %u,\n"
        "  \"imu_state\": %u,\n"
        "  \"imu_averaging_time_ms\": %u,\n"
        "  \"power_rail_flags\": %u,\n"
        "  \"channel_slots\": [1, 1, 1, 1, 2, 2, 2, 2],\n"
        "  \"channel_inputs\": [0, 1, 2, 3, 0, 1, 2, 3],\n"
        "  \"descriptor_size_bytes\": %u,\n"
        "  \"checkpoint_offset_bytes\": %" PRIu32 ",\n"
        "  \"checkpoint_entry_size_bytes\": %u,\n"
        "  \"checkpoint_capacity\": %u,\n"
        "  \"checkpoint_interval_ms\": %" PRIu32 ",\n"
        "  \"data_offset_bytes\": %" PRIu32 ",\n"
        "  \"record_size_bytes\": 512,\n"
        "  \"sample_encoding\": \"signed24_le\",\n"
        "  \"header_crc32\": \"00000000\"\n"
        "}\n",
        (unsigned)RECORDING_HEADER_FORMAT_VERSION,
        RECORDING_HEADER_TOOLS_URL,
        metadata->recording_name,
        metadata->device_id[0], metadata->device_id[1],
        metadata->device_id[2], metadata->device_id[3],
        metadata->device_id[4], metadata->device_id[5],
        (unsigned)metadata->hardware_version,
        (unsigned)metadata->hardware_revision,
        metadata->firmware_version,
        (unsigned)metadata->protocol_version,
        metadata->start_monotonic_timestamp_100ns,
        metadata->start_unix_timestamp_us,
        metadata->adc_sample_rate_sps,
        UINT32_C(10000000) / metadata->adc_sample_rate_sps,
        (unsigned)metadata->adc_channel_mask,
        (unsigned)channel_count(metadata->adc_channel_mask),
        (unsigned)metadata->adc_gains[0], (unsigned)metadata->adc_gains[1],
        (unsigned)metadata->adc_gains[2], (unsigned)metadata->adc_gains[3],
        (unsigned)metadata->adc_gains[4], (unsigned)metadata->adc_gains[5],
        (unsigned)metadata->adc_gains[6], (unsigned)metadata->adc_gains[7],
        (unsigned)packed_gain,
        (unsigned)metadata->card_types[0],
        (unsigned)metadata->card_types[1],
        (unsigned)metadata->gnss_state,
        (unsigned)metadata->imu_state,
        (unsigned)metadata->imu_averaging_time_ms,
        (unsigned)metadata->power_rail_flags,
        (unsigned)RECORDING_HEADER_STATIC_SIZE_BYTES,
        RECORDING_HEADER_CHECKPOINT_OFFSET_BYTES,
        (unsigned)RECORDING_HEADER_CHECKPOINT_SIZE_BYTES,
        (unsigned)RECORDING_HEADER_CHECKPOINT_CAPACITY,
        RECORDING_HEADER_CHECKPOINT_INTERVAL_MS,
        RECORDING_HEADER_DATA_OFFSET_BYTES);
    if (used < 0 || used >= RECORDING_HEADER_STATIC_SIZE_BYTES - 1) {
        return RECORDING_HEADER_CODEC_INVALID_FIELD;
    }

    const char *crc_field = "\"header_crc32\": \"";
    char *digits = strstr((char *)header, crc_field);
    if (digits == NULL) {
        return RECORDING_HEADER_CODEC_INVALID_FIELD;
    }
    digits += strlen(crc_field);
    header[used] = ' '; /* JSON has whitespace padding, never a NUL byte. */
    header[RECORDING_HEADER_STATIC_SIZE_BYTES - 1U] = '\n';
    const uint32_t checksum = crc32(
        crc_context, header, RECORDING_HEADER_STATIC_SIZE_BYTES);
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t index = 0U; index < 8U; index++) {
        digits[index] = hex[(checksum >> (28U - 4U * index)) & UINT32_C(0xF)];
    }
    return RECORDING_HEADER_CODEC_OK;
}
