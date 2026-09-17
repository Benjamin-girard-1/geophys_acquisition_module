#ifndef GEOPHYS_ADC_RECORD_H
#define GEOPHYS_ADC_RECORD_H

#include <stdint.h>

/*
 * Compile-only scaffold for the proposed portable 512-byte ADC record.
 *
 * This component will not know about AD7779 registers, application queues,
 * UART, ESP-IDF, FatFs, or SD hardware. The proposed layout is documented in
 * shared/protocol/adc_record.md and remains subject to review.
 */
#define ADC_RECORD_SIZE_BYTES                 UINT16_C(512)
#define ADC_RECORD_HEADER_SIZE_BYTES          UINT16_C(28)
#define ADC_RECORD_PAYLOAD_SIZE_BYTES         UINT16_C(480)
#define ADC_RECORD_CRC_SIZE_BYTES             UINT16_C(4)
#define ADC_RECORD_PAYLOAD_OFFSET_BYTES       UINT16_C(28)
#define ADC_RECORD_CRC_OFFSET_BYTES           UINT16_C(508)
#define ADC_RECORD_CHANNEL_COUNT              UINT8_C(8)
#define ADC_RECORD_PACKED_SAMPLE_SIZE_BYTES   UINT8_C(3)

typedef uint8_t adc_record_flags_t;

/**
 * @brief Logical metadata proposed for one ADC record.
 *
 * This type is convenient input/output state only. Its compiler layout is not
 * the persistent or wire representation and must never be copied into a record.
 */
typedef struct {
    uint8_t format_version;
    uint8_t channel_mask;
    uint8_t conversion_count;
    adc_record_flags_t aggregate_flags;
    uint32_t applied_sample_rate_sps;
    uint64_t first_conversion_sequence;
    uint64_t first_monotonic_timestamp_us;
} adc_record_metadata_t;

/*
 * Planned after format approval:
 * - stateful builder begin/append/finalize operations;
 * - complete-record validation and metadata decoding;
 * - signed 24-bit pack/unpack helpers;
 * - CRC-32C and canonical-padding validation.
 *
 * Function signatures are intentionally withheld until timestamp, gap, and
 * rare-error policies are approved.
 */

#endif /* GEOPHYS_ADC_RECORD_H */
