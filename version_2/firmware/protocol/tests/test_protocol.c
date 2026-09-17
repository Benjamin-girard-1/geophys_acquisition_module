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
    assert(argc == 4);
    uint8_t hello[PROTOCOL_COMMAND_SIZE_BYTES];
    uint8_t device_info[PROTOCOL_COMMAND_SIZE_BYTES];
    uint8_t bad_crc[PROTOCOL_COMMAND_SIZE_BYTES];
    load_hex_vector(argv[1], hello);
    load_hex_vector(argv[2], device_info);
    load_hex_vector(argv[3], bad_crc);

    test_crc_check_value();
    test_hello_decode(hello);
    test_canonical_validation(hello);
    test_device_info_encode(device_info);
    test_fragmentation(hello);
    test_garbage_concatenation_and_recovery(hello, bad_crc);
    assert(protocol_command_decode(
               bad_crc, reference_crc32, NULL,
               &(protocol_command_t){0}) == PROTOCOL_FRAME_INVALID_CRC);

    puts("protocol tests passed");
    return EXIT_SUCCESS;
}
