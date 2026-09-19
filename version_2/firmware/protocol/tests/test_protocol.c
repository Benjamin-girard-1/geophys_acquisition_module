#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol_frame.h"
#include "protocol_messages.h"

typedef struct {
    size_t valid_count;
    size_t invalid_crc_count;
    protocol_command_t last_command;
} parser_observation_t;

static uint32_t reference_crc32(void *context,
                                const uint8_t *data,
                                size_t length_bytes)
{
    (void)context;
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t index = 0U; index < length_bytes; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            const uint32_t reflected_polynomial =
                (crc & 1U) ? UINT32_C(0xEDB88320) : 0U;
            crc = (crc >> 1U) ^ reflected_polynomial;
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static uint8_t hex_nibble(int character)
{
    if ((character >= '0') && (character <= '9')) {
        return (uint8_t)(character - '0');
    }
    character = tolower(character);
    assert((character >= 'a') && (character <= 'f'));
    return (uint8_t)(character - 'a' + 10);
}

static void load_hex_vector(const char *path,
                            uint8_t bytes[PROTOCOL_COMMAND_SIZE_BYTES])
{
    FILE *file = fopen(path, "r");
    assert(file != NULL);

    size_t byte_count = 0U;
    int high_nibble = -1;
    for (;;) {
        const int character = fgetc(file);
        if (character == EOF) {
            break;
        }
        if (isspace(character)) {
            continue;
        }
        assert(isxdigit(character));
        if (high_nibble < 0) {
            high_nibble = hex_nibble(character);
        } else {
            assert(byte_count < PROTOCOL_COMMAND_SIZE_BYTES);
            bytes[byte_count] =
                (uint8_t)(((uint8_t)high_nibble << 4U) |
                          hex_nibble(character));
            byte_count++;
            high_nibble = -1;
        }
    }
    assert(fclose(file) == 0);
    assert(high_nibble < 0);
    assert(byte_count == PROTOCOL_COMMAND_SIZE_BYTES);
}

static void observe_parser_event(void *context,
                                 protocol_frame_status_t status,
                                 const protocol_command_t *command)
{
    parser_observation_t *observation = context;
    if (status == PROTOCOL_FRAME_OK) {
        assert(command != NULL);
        observation->valid_count++;
        observation->last_command = *command;
    } else if (status == PROTOCOL_FRAME_INVALID_CRC) {
        assert(command == NULL);
        observation->invalid_crc_count++;
    }
}

static void test_crc_check_value(void)
{
    static const uint8_t check[] = "123456789";
    assert(reference_crc32(NULL, check, sizeof(check) - 1U) ==
           UINT32_C(0xCBF43926));
}

static void test_hello_decode(const uint8_t hello[PROTOCOL_COMMAND_SIZE_BYTES])
{
    protocol_command_t command;
    assert(protocol_command_decode(
               hello, reference_crc32, NULL, &command) == PROTOCOL_FRAME_OK);
    assert(protocol_decode_hello_request(&command) == PROTOCOL_MESSAGE_OK);
}

static void rewrite_crc(uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES])
{
    const uint32_t crc = reference_crc32(
        NULL, frame, PROTOCOL_COMMAND_CRC_OFFSET_BYTES);
    frame[60] = (uint8_t)crc;
    frame[61] = (uint8_t)(crc >> 8U);
    frame[62] = (uint8_t)(crc >> 16U);
    frame[63] = (uint8_t)(crc >> 24U);
}

static void test_canonical_validation(
    const uint8_t hello[PROTOCOL_COMMAND_SIZE_BYTES])
{
    protocol_command_t command;
    uint8_t candidate[PROTOCOL_COMMAND_SIZE_BYTES];

    memcpy(candidate, hello, sizeof(candidate));
    candidate[PROTOCOL_COMMAND_DIRECTION_OFFSET_BYTES] = 2U;
    rewrite_crc(candidate);
    assert(protocol_command_decode(
               candidate, reference_crc32, NULL, &command) ==
           PROTOCOL_FRAME_INVALID_DIRECTION);

    memcpy(candidate, hello, sizeof(candidate));
    candidate[PROTOCOL_COMMAND_RESERVED_OFFSET_BYTES] = 1U;
    rewrite_crc(candidate);
    assert(protocol_command_decode(
               candidate, reference_crc32, NULL, &command) ==
           PROTOCOL_FRAME_INVALID_RESERVED);

    memcpy(candidate, hello, sizeof(candidate));
    candidate[PROTOCOL_COMMAND_LENGTH_OFFSET_BYTES] = 49U;
    rewrite_crc(candidate);
    assert(protocol_command_decode(
               candidate, reference_crc32, NULL, &command) ==
           PROTOCOL_FRAME_INVALID_LENGTH);

    memcpy(candidate, hello, sizeof(candidate));
    candidate[PROTOCOL_COMMAND_PAYLOAD_OFFSET_BYTES] = 1U;
    rewrite_crc(candidate);
    assert(protocol_command_decode(
               candidate, reference_crc32, NULL, &command) ==
           PROTOCOL_FRAME_INVALID_PADDING);
}

static void test_device_info_encode(
    const uint8_t expected[PROTOCOL_COMMAND_SIZE_BYTES])
{
    const protocol_device_info_t info = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .mac_address = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56},
        .hardware_version = 2U,
        .hardware_revision = 1U,
        .firmware_version = UINT32_C(0x01020304),
        .protocol_version = PROTOCOL_VERSION_CURRENT,
    };
    uint8_t actual[PROTOCOL_COMMAND_SIZE_BYTES];
    assert(protocol_encode_device_info_reply(
               &info, reference_crc32, NULL, actual) ==
           PROTOCOL_MESSAGE_OK);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
}

static void test_device_config_requests(
    const uint8_t get_config[PROTOCOL_COMMAND_SIZE_BYTES],
    const uint8_t set_config[PROTOCOL_COMMAND_SIZE_BYTES])
{
    protocol_command_t command;
    assert(protocol_command_decode(
               get_config, reference_crc32, NULL,
               &command) == PROTOCOL_FRAME_OK);
    assert(protocol_decode_device_get_config_request(&command) ==
           PROTOCOL_MESSAGE_OK);

    assert(protocol_command_decode(
               set_config, reference_crc32, NULL,
               &command) == PROTOCOL_FRAME_OK);
    protocol_device_config_update_t update;
    assert(protocol_decode_device_set_config_request(&command, &update) ==
           PROTOCOL_MESSAGE_OK);
    assert(update.adc_sample_rate == PROTOCOL_ADC_SAMPLE_RATE_2000_SPS);
    assert(update.adc_channel_mask == PROTOCOL_ADC_CHANNEL_MASK_LOW);
    assert(update.adc_gain == UINT16_C(0xE4E4));
    assert(update.rail_3v3_enabled == 0U);
    assert(update.rail_5v_enabled == 0U);
    assert(update.rail_9v_enabled == 0U);
    assert(update.rail_negative_5v_enabled == 0U);
    assert(update.rail_18v_enabled == 0U);
    assert(update.imu_averaging_time_ms == 0U);

    command.payload[13] = UINT8_C(0x03);
    assert(protocol_decode_device_set_config_request(&command, &update) ==
           PROTOCOL_MESSAGE_INVALID_FIELD);
}

static void test_device_config_encode(
    const uint8_t expected[PROTOCOL_COMMAND_SIZE_BYTES])
{
    const protocol_device_config_t config = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .timestamp_100ns = UINT64_C(0x0102030405060708),
        .recording_in_progress = 0U,
        .card_slot_1 = PROTOCOL_CARD_MAGNETIC,
        .card_slot_2 = PROTOCOL_CARD_ACC_GEOPH,
        .adc_sample_rate = PROTOCOL_ADC_SAMPLE_RATE_2000_SPS,
        .adc_channel_mask = PROTOCOL_ADC_CHANNEL_MASK_LOW,
        .adc_gain = UINT16_C(0xE4E4),
        .adc_temperature_centi_c = -1234,
        .rail_3v3_enabled = 1U,
        .rail_5v_enabled = 0U,
        .rail_9v_enabled = 1U,
        .rail_negative_5v_enabled = 1U,
        .rail_18v_enabled = 0U,
        .solar_present = 1U,
        .usb_5v_present = 1U,
        .gnss_state = PROTOCOL_GNSS_SEARCHING,
        .gnss_satellite_count = 12U,
        .imu_state = PROTOCOL_IMU_READY,
        .imu_averaging_time_ms = 250U,
        .imu_roll_centi_degrees = -123,
        .imu_pitch_centi_degrees = 456,
        .imu_temperature_centi_c = 2500,
        .sd_card_state = PROTOCOL_SD_CARD_PRESENT,
        .esp32_temperature_centi_c = 4200,
        .error_pending = 1U,
    };
    uint8_t actual[PROTOCOL_COMMAND_SIZE_BYTES];
    assert(protocol_encode_device_config_reply(
               &config, reference_crc32, NULL, actual) ==
           PROTOCOL_MESSAGE_OK);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
}

static void test_fragmentation(
    const uint8_t hello[PROTOCOL_COMMAND_SIZE_BYTES])
{
    for (size_t split = 0U; split <= PROTOCOL_COMMAND_SIZE_BYTES; split++) {
        protocol_command_parser_t parser;
        parser_observation_t observation = {0};
        assert(protocol_command_parser_initialize(
                   &parser, reference_crc32, NULL) == PROTOCOL_FRAME_OK);
        (void)protocol_command_parser_feed(
            &parser, hello, split, observe_parser_event, &observation);
        (void)protocol_command_parser_feed(
            &parser, hello + split,
            PROTOCOL_COMMAND_SIZE_BYTES - split,
            observe_parser_event, &observation);
        assert(observation.valid_count == 1U);
        assert(observation.invalid_crc_count == 0U);
    }
}

static void test_streaming_messages(void)
{
    protocol_command_t request = {
        .command_id = PROTOCOL_COMMAND_STREAMING_START,
        .direction = PROTOCOL_DIRECTION_TO_DEVICE,
        .payload_length = PROTOCOL_STREAMING_START_PAYLOAD_SIZE_BYTES,
        .payload = {
            PROTOCOL_STREAMING_DECIMATION_5,
            PROTOCOL_ADC_CHANNEL_MASK_ALL,
        },
    };
    protocol_streaming_start_request_t start_request;
    assert(protocol_decode_streaming_start_request(
               &request, &start_request) == PROTOCOL_MESSAGE_OK);
    assert(start_request.decimation == PROTOCOL_STREAMING_DECIMATION_5);
    assert(start_request.channel_mask == PROTOCOL_ADC_CHANNEL_MASK_ALL);
    request.payload[0] = 3U;
    assert(protocol_decode_streaming_start_request(
               &request, &start_request) == PROTOCOL_MESSAGE_INVALID_FIELD);

    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES];
    protocol_command_t decoded;
    const protocol_streaming_start_result_t start_result = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .decimation = PROTOCOL_STREAMING_DECIMATION_5,
        .channel_mask = PROTOCOL_ADC_CHANNEL_MASK_ALL,
        .recording_in_progress = 1U,
    };
    assert(protocol_encode_streaming_start_reply(
               &start_result, reference_crc32, NULL, frame) ==
           PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_STREAMING_START_RESULT);
    assert(decoded.payload_length ==
           PROTOCOL_STREAMING_START_REPLY_PAYLOAD_SIZE_BYTES);
    assert(decoded.payload[1] == PROTOCOL_STREAMING_DECIMATION_5);
    assert(decoded.payload[2] == PROTOCOL_ADC_CHANNEL_MASK_ALL);
    assert(decoded.payload[3] == 1U);

    request.command_id = PROTOCOL_COMMAND_STREAMING_STOP;
    request.payload_length = 0U;
    memset(request.payload, 0, sizeof(request.payload));
    assert(protocol_decode_streaming_stop_request(&request) ==
           PROTOCOL_MESSAGE_OK);

    const protocol_streaming_stop_result_t stop_result = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .recording_in_progress = 0U,
    };
    assert(protocol_encode_streaming_stop_reply(
               &stop_result, reference_crc32, NULL, frame) ==
           PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_STREAMING_STOP_RESULT);
    assert(decoded.payload_length ==
           PROTOCOL_STREAMING_STOP_REPLY_PAYLOAD_SIZE_BYTES);
}

static void test_recording_messages(void)
{
    protocol_command_t request = {
        .command_id = PROTOCOL_COMMAND_RECORDING_START,
        .direction = PROTOCOL_DIRECTION_TO_DEVICE,
        .payload_length = PROTOCOL_RECORDING_START_PAYLOAD_SIZE_BYTES,
        .payload = {'F', 'i', 'e', 'l', 'd', '_', '0', '1', 0},
    };
    protocol_recording_name_t name;
    assert(protocol_decode_recording_start_request(&request, &name) ==
           PROTOCOL_MESSAGE_OK);
    assert(strcmp(name.bytes, "field_01") == 0);
    request.payload[9] = 1U;
    assert(protocol_decode_recording_start_request(&request, &name) ==
           PROTOCOL_MESSAGE_INVALID_FIELD);
    request.payload[9] = 0U;

    uint8_t frame[PROTOCOL_COMMAND_SIZE_BYTES];
    protocol_command_t decoded;
    protocol_recording_start_result_t start = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .recording_in_progress = 1U,
        .name = {.bytes = "field_01"},
    };
    assert(protocol_encode_recording_start_reply(
               &start, reference_crc32, NULL, frame) ==
           PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_RECORDING_START_RESULT);
    assert(decoded.payload_length == 34U);

    protocol_recording_stop_result_t stop = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .name = {.bytes = "field_01"},
    };
    assert(protocol_encode_recording_stop_reply(
               &stop, reference_crc32, NULL, frame) == PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_RECORDING_STOP_RESULT);
    assert(decoded.payload_length == 33U);

    protocol_recording_number_t number = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .recording_count = 255U,
    };
    assert(protocol_encode_recording_number_reply(
               &number, reference_crc32, NULL, frame) ==
           PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_RECORDING_NUMBER);
    assert(decoded.payload[1] == 0xFFU && decoded.payload[2] == 0U);

    request.command_id = PROTOCOL_COMMAND_RECORDING_GET_INFO;
    request.payload_length = 2U;
    request.payload[0] = 0x34U;
    request.payload[1] = 0x12U;
    uint16_t index = 0U;
    assert(protocol_decode_recording_get_info_request(&request, &index) ==
           PROTOCOL_MESSAGE_OK);
    assert(index == UINT16_C(0x1234));

    protocol_recording_info_t info = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .recording_index = UINT16_C(0x1234),
        .recording_in_progress = 1U,
        .name = {.bytes = "field_01"},
        .start_unix_timestamp_us = UINT64_C(0x0102030405060708),
        .size_bytes = UINT32_C(0x11223344),
    };
    assert(protocol_encode_recording_info_reply(
               &info, reference_crc32, NULL, frame) == PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_RECORDING_INFO);
    assert(decoded.payload_length == 48U);

    protocol_recording_delete_result_t deleted = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .recording_in_progress = 0U,
        .name = {.bytes = "field_01"},
    };
    assert(protocol_encode_recording_delete_reply(
               &deleted, reference_crc32, NULL, frame) ==
           PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_RECORDING_DELETE_RESULT);

    request.command_id = PROTOCOL_COMMAND_TEMP_RECORDING_READ;
    request.payload_length =
        PROTOCOL_TEMP_RECORDING_READ_REQUEST_PAYLOAD_SIZE_BYTES;
    memset(request.payload, 0, sizeof(request.payload));
    memcpy(request.payload, "Field_01", sizeof("Field_01"));
    request.payload[32] = 0x78U;
    request.payload[33] = 0x56U;
    request.payload[34] = 0x34U;
    request.payload[35] = 0x12U;
    protocol_temp_recording_read_request_t read_request;
    assert(protocol_decode_temp_recording_read_request(
               &request, &read_request) == PROTOCOL_MESSAGE_OK);
    assert(strcmp(read_request.name.bytes, "field_01") == 0);
    assert(read_request.offset_bytes == UINT32_C(0x12345678));

    protocol_temp_recording_read_reply_t read_reply = {
        .result = PROTOCOL_RESULT_SUCCESS,
        .file_size_bytes = UINT32_C(1024),
        .offset_bytes = UINT32_C(512),
        .data_length_bytes = PROTOCOL_TEMP_RECORDING_READ_DATA_SIZE_BYTES,
    };
    for (uint8_t data_index = 0U;
         data_index < PROTOCOL_TEMP_RECORDING_READ_DATA_SIZE_BYTES;
         data_index++) {
        read_reply.data[data_index] = data_index;
    }
    assert(protocol_encode_temp_recording_read_reply(
               &read_reply, reference_crc32, NULL, frame) ==
           PROTOCOL_MESSAGE_OK);
    assert(protocol_command_decode(frame, reference_crc32, NULL, &decoded) ==
           PROTOCOL_FRAME_OK);
    assert(decoded.command_id == PROTOCOL_REPLY_TEMP_RECORDING_READ);
    assert(decoded.direction == PROTOCOL_DIRECTION_TO_HOST);
    assert(decoded.payload_length ==
           PROTOCOL_TEMP_RECORDING_READ_REPLY_PAYLOAD_SIZE_BYTES);
    assert(decoded.payload[9] ==
           PROTOCOL_TEMP_RECORDING_READ_DATA_SIZE_BYTES);
    assert(memcmp(decoded.payload + 10U, read_reply.data,
                  sizeof(read_reply.data)) == 0);
}

static void test_garbage_concatenation_and_recovery(
    const uint8_t hello[PROTOCOL_COMMAND_SIZE_BYTES],
    const uint8_t bad_crc[PROTOCOL_COMMAND_SIZE_BYTES])
{
    static const uint8_t garbage[] = {
        0x00, 0xFF, '\\', 'C', 'x', '\\',
    };
    protocol_command_parser_t parser;
    parser_observation_t observation = {0};
    assert(protocol_command_parser_initialize(
               &parser, reference_crc32, NULL) == PROTOCOL_FRAME_OK);

    (void)protocol_command_parser_feed(
        &parser, garbage, sizeof(garbage),
        observe_parser_event, &observation);
    (void)protocol_command_parser_feed(
        &parser, bad_crc, PROTOCOL_COMMAND_SIZE_BYTES,
        observe_parser_event, &observation);
    (void)protocol_command_parser_feed(
        &parser, hello, PROTOCOL_COMMAND_SIZE_BYTES,
        observe_parser_event, &observation);
    (void)protocol_command_parser_feed(
        &parser, hello, PROTOCOL_COMMAND_SIZE_BYTES,
        observe_parser_event, &observation);

    assert(observation.invalid_crc_count == 1U);
    assert(observation.valid_count == 2U);
}

int main(int argc, char **argv)
{
    assert(argc == 7);
    uint8_t hello[PROTOCOL_COMMAND_SIZE_BYTES];
    uint8_t device_info[PROTOCOL_COMMAND_SIZE_BYTES];
    uint8_t bad_crc[PROTOCOL_COMMAND_SIZE_BYTES];
    uint8_t get_config[PROTOCOL_COMMAND_SIZE_BYTES];
    uint8_t set_config[PROTOCOL_COMMAND_SIZE_BYTES];
    uint8_t device_config[PROTOCOL_COMMAND_SIZE_BYTES];
    load_hex_vector(argv[1], hello);
    load_hex_vector(argv[2], device_info);
    load_hex_vector(argv[3], bad_crc);
    load_hex_vector(argv[4], get_config);
    load_hex_vector(argv[5], set_config);
    load_hex_vector(argv[6], device_config);

    test_crc_check_value();
    test_hello_decode(hello);
    test_canonical_validation(hello);
    test_device_info_encode(device_info);
    test_device_config_requests(get_config, set_config);
    test_device_config_encode(device_config);
    test_streaming_messages();
    test_recording_messages();
    test_fragmentation(hello);
    test_garbage_concatenation_and_recovery(hello, bad_crc);
    assert(protocol_command_decode(
               bad_crc, reference_crc32, NULL,
               &(protocol_command_t){0}) == PROTOCOL_FRAME_INVALID_CRC);

    puts("protocol tests passed");
    return EXIT_SUCCESS;
}
