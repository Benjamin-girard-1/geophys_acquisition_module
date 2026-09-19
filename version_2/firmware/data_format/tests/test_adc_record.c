#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adc_record.h"

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
    assert(adc_record_builder_finalize(
               &builder, reference_crc32, NULL) == ADC_RECORD_CODEC_OK);
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

int main(int argc, char **argv)
{
    assert(argc == 2);
    uint8_t vector[ADC_RECORD_SIZE_BYTES];
    load_hex_vector(argv[1], vector);
    test_shared_vector(vector);
    test_sample_extremes();
    test_four_channel_and_invalid_records();
    puts("ADC record tests passed");
    return EXIT_SUCCESS;
}
