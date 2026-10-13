#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adc_record.h"
#include "recording_header.h"

static uint32_t reference_crc32(void *context,
                                const uint8_t *data,
                                size_t length_bytes)
{
    (void)context;
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t index = 0U; index < length_bytes; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^
                  ((crc & 1U) ? UINT32_C(0xEDB88320) : 0U);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static uint8_t hex_nibble(int character)
{
    if (character >= '0' && character <= '9') {
        return (uint8_t)(character - '0');
    }
    character = tolower(character);
    assert(character >= 'a' && character <= 'f');
    return (uint8_t)(character - 'a' + 10);
}

static void load_hex_vector(const char *path,
                            uint8_t bytes[ADC_RECORD_SIZE_BYTES])
{
    FILE *file = fopen(path, "r");
    assert(file != NULL);
    size_t count = 0U;
    int high = -1;
    for (;;) {
        const int character = fgetc(file);
        if (character == EOF) {
            break;
        }
        if (isspace(character)) {
            continue;
        }
        assert(isxdigit(character));
        if (high < 0) {
            high = hex_nibble(character);
        } else {
            assert(count < ADC_RECORD_SIZE_BYTES);
            bytes[count++] = (uint8_t)(((uint8_t)high << 4U) |
                                        hex_nibble(character));
            high = -1;
        }
    }
    assert(fclose(file) == 0);
    assert(high < 0);
    assert(count == ADC_RECORD_SIZE_BYTES);
}

static void test_shared_vector(const uint8_t expected[ADC_RECORD_SIZE_BYTES])
{
    uint8_t actual[ADC_RECORD_SIZE_BYTES];
    memset(actual, 0xA5, sizeof(actual));
    adc_record_builder_t builder;
    const adc_record_metadata_t metadata = {
        .channel_mask = UINT8_C(0xFF),
        .status = ADC_RECORD_STATUS_OK,
        .packed_gain = UINT16_C(0xE4E4),
        .payload_number = UINT32_C(0x01020304),
        .first_conversion_sequence = 0U,
        .first_monotonic_timestamp_100ns = 0U,
        .sample_period_100ns = UINT32_C(10000),
    };
    assert(adc_record_builder_begin(&builder, actual, &metadata) ==
           ADC_RECORD_CODEC_OK);
    for (uint32_t conversion = 0U; conversion < 20U; conversion++) {
        int32_t samples[ADC_RECORD_CHANNEL_COUNT];
        for (uint32_t channel = 0U;
             channel < ADC_RECORD_CHANNEL_COUNT;
             channel++) {
            samples[channel] =
                (int32_t)(conversion * ADC_RECORD_CHANNEL_COUNT + channel) -
                80;
        }
        assert(adc_record_builder_append(
                   &builder,
                   UINT32_C(0x11223344) + conversion,
                   UINT64_C(0x0102030405060708) + conversion * 10000U,
                   samples) == ADC_RECORD_CODEC_OK);
    }
    assert(adc_record_builder_complete(&builder) == ADC_RECORD_CODEC_OK);
    for (size_t index = ADC_RECORD_CRC_OFFSET_BYTES;
         index < ADC_RECORD_SIZE_BYTES;
         index++) {
        assert(actual[index] == UINT8_C(0xA5));
    }
    assert(adc_record_write_crc(actual, reference_crc32, NULL) ==
           ADC_RECORD_CODEC_OK);
    assert(memcmp(actual, expected, ADC_RECORD_SIZE_BYTES) == 0);

    adc_record_metadata_t decoded;
    assert(adc_record_validate(actual, reference_crc32, NULL, &decoded) ==
           ADC_RECORD_CODEC_OK);
    assert(decoded.channel_mask == UINT8_C(0xFF));
    assert(decoded.packed_gain == UINT16_C(0xE4E4));
    assert(decoded.payload_number == UINT32_C(0x01020304));
    assert(decoded.first_conversion_sequence == UINT32_C(0x11223344));
    assert(decoded.first_monotonic_timestamp_100ns ==
           UINT64_C(0x0102030405060708));
    assert(decoded.sample_period_100ns == UINT32_C(10000));
}

static void test_sample_extremes(void)
{
    static const int32_t values[] = {
        -INT32_C(8388608), -1, 0, 1, INT32_C(8388607),
    };
    for (size_t index = 0U;
         index < sizeof(values) / sizeof(values[0]);
         index++) {
        uint8_t packed[3];
        assert(adc_record_pack_sample(values[index], packed) ==
               ADC_RECORD_CODEC_OK);
        assert(adc_record_unpack_sample(packed) == values[index]);
    }
    uint8_t packed[3];
    assert(adc_record_pack_sample(INT32_C(8388608), packed) ==
           ADC_RECORD_CODEC_INVALID_FIELD);
    assert(adc_record_pack_sample(-INT32_C(8388609), packed) ==
           ADC_RECORD_CODEC_INVALID_FIELD);
}

static void test_four_channel_and_invalid_records(void)
{
    uint8_t record[ADC_RECORD_SIZE_BYTES];
    adc_record_builder_t builder;
    const adc_record_metadata_t metadata = {
        .channel_mask = UINT8_C(0x0F),
        .status = ADC_RECORD_STATUS_TIMING_ERROR,
        .packed_gain = 0U,
        .payload_number = 7U,
        .sample_period_100ns = 20000U,
    };
    assert(adc_record_builder_begin(&builder, record, &metadata) ==
           ADC_RECORD_CODEC_OK);
    int32_t samples[ADC_RECORD_CHANNEL_COUNT] = {0};
    assert(adc_record_builder_append(&builder, 1U, 2U, samples) ==
           ADC_RECORD_CODEC_OK);
    assert(adc_record_builder_finalize(&builder, reference_crc32, NULL) ==
           ADC_RECORD_CODEC_INCOMPLETE);
    for (uint32_t conversion = 1U; conversion < 40U; conversion++) {
        assert(adc_record_builder_append(
                   &builder, 1U + conversion,
                   2U + conversion * 20000U, samples) ==
               ADC_RECORD_CODEC_OK);
    }
    assert(adc_record_builder_finalize(&builder, reference_crc32, NULL) ==
           ADC_RECORD_CODEC_OK);
    record[100] ^= 1U;
    assert(adc_record_validate(record, reference_crc32, NULL, NULL) ==
           ADC_RECORD_CODEC_INTEGRITY);
}

static void test_upper_four_channel_order(void)
{
    uint8_t record[ADC_RECORD_SIZE_BYTES];
    adc_record_builder_t builder;
    const adc_record_metadata_t metadata = {
        .channel_mask = UINT8_C(0xF0),
        .status = ADC_RECORD_STATUS_OK,
        .packed_gain = 0U,
        .payload_number = 8U,
        .sample_period_100ns = 625U,
    };
    const int32_t samples[ADC_RECORD_CHANNEL_COUNT] = {
        INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX,
        -INT32_C(8388608), -1, INT32_C(0x123456), INT32_C(8388607),
    };
    static const uint8_t expected_first_conversion[] = {
        0x00, 0x00, 0x80,
        0xFF, 0xFF, 0xFF,
        0x56, 0x34, 0x12,
        0xFF, 0xFF, 0x7F,
    };

    assert(adc_record_builder_begin(&builder, record, &metadata) ==
           ADC_RECORD_CODEC_OK);
    assert(builder.first_channel == 4U);
    assert(builder.channel_count == 4U);
    for (uint32_t conversion = 0U; conversion < 40U; conversion++) {
        adc_record_builder_append_trusted(
            &builder, conversion, conversion * 625U, samples);
    }
    assert(memcmp(record + ADC_RECORD_PAYLOAD_OFFSET_BYTES,
                  expected_first_conversion,
                  sizeof(expected_first_conversion)) == 0);
    assert(adc_record_builder_finalize(
               &builder, reference_crc32, NULL) == ADC_RECORD_CODEC_OK);
}

static void test_recording_header(const char *output_path)
{
    const recording_header_metadata_t metadata = {
        .recording_name = "field007",
        .device_id = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
        .hardware_version = 2U,
        .hardware_revision = 1U,
        .firmware_version = UINT32_C(0x01020304),
        .protocol_version = 1U,
        .start_monotonic_timestamp_100ns =
            UINT64_C(0x0102030405060708),
        .start_unix_timestamp_us = 0U,
        .adc_sample_rate_sps = 1000U,
        .adc_channel_mask = UINT8_C(0x0F),
        .adc_gains = {8U, 8U, 8U, 1U, 1U, 1U, 1U, 1U},
        .card_types = {1U, 0U},
        .gnss_state = 0U,
        .imu_state = 0U,
        .imu_averaging_time_ms = 0U,
        .power_rail_flags = RECORDING_HEADER_POWER_3V3_ENABLED |
                            RECORDING_HEADER_POWER_5V_ENABLED,
    };
    uint8_t header[RECORDING_HEADER_STATIC_SIZE_BYTES];
    assert(recording_header_encode_static(
               &metadata, reference_crc32, NULL, header) ==
           RECORDING_HEADER_CODEC_OK);
    assert(header[0] == '{');
    for (size_t index = 0U; index < sizeof(header); index++) {
        assert(header[index] == '\n' ||
               (header[index] >= ' ' && header[index] <= '~'));
    }
    char text[RECORDING_HEADER_STATIC_SIZE_BYTES + 1U];
    memcpy(text, header, sizeof(header));
    text[sizeof(header)] = '\0';
    assert(strstr(text, RECORDING_HEADER_TOOLS_URL) != NULL);
    assert(strstr(text, "\"adc_gains\": [8, 8, 8, 1, 1, 1, 1, 1]") != NULL);
    assert(strstr(text, "\"data_offset_bytes\": 32768") != NULL);
    char *digits = strstr(text, "\"header_crc32\": \"");
    assert(digits != NULL);
    digits += strlen("\"header_crc32\": \"");
    const uint32_t expected_crc = (uint32_t)strtoul(digits, NULL, 16);
    memcpy(digits, "00000000", 8U);
    assert(reference_crc32(NULL, (const uint8_t *)text,
                           sizeof(header)) == expected_crc);

    if (output_path != NULL) {
        FILE *output = fopen(output_path, "wb");
        assert(output != NULL);
        assert(fwrite(header, 1U, sizeof(header), output) == sizeof(header));
        assert(fclose(output) == 0);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    uint8_t vector[ADC_RECORD_SIZE_BYTES];
    load_hex_vector(argv[1], vector);
    test_shared_vector(vector);
    test_sample_extremes();
    test_four_channel_and_invalid_records();
    test_upper_four_channel_order();
    test_recording_header(argc == 3 ? argv[2] : NULL);
    puts("ADC record tests passed");
    return EXIT_SUCCESS;
}
