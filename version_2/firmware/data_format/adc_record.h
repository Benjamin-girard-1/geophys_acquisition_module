#ifndef GEOPHYS_ADC_RECORD_H
#define GEOPHYS_ADC_RECORD_H

#include <stdint.h>

/*
 * Compile-only scaffold for the portable 512-byte data block defined in
 * shared/protocol/protocol.md.
 *
 * This component will not know about AD7779 registers, application queues,
 * UART, ESP-IDF, FatFs, or SD hardware.
 */
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

/*
 * Planned against shared test vectors:
 * - stateful builder begin/append/finalize operations;
 * - complete-record validation and metadata decoding;
 * - signed 24-bit pack/unpack helpers;
 * - CRC-32/ISO-HDLC validation.
 */

#endif /* GEOPHYS_ADC_RECORD_H */
