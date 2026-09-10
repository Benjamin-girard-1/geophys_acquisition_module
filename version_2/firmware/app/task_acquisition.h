#ifndef GEOPHYS_TASK_ACQUISITION_H
#define GEOPHYS_TASK_ACQUISITION_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_error.h"
#include "fw_time.h"

#define ADC_FRAME_CHANNEL_COUNT 8U
#define ADC_BLOCK_FRAME_CAPACITY 32U

/** @brief Sequence assigned to every expected simultaneous ADC conversion. */
typedef uint64_t adc_sequence_t;

/** @brief Normalized conditions associated with one ADC conversion frame. */
typedef enum {
    ADC_FRAME_STATUS_NONE = 0,
    ADC_FRAME_STATUS_DEVICE_ALERT = (1U << 0),
    ADC_FRAME_STATUS_UNEXPECTED_RESET = (1U << 1),
    ADC_FRAME_STATUS_CHANNEL_SATURATION = (1U << 2),
    ADC_FRAME_STATUS_CHANNEL_INPUT_ERROR = (1U << 3),
    ADC_FRAME_STATUS_DATA_CRC_ERROR = (1U << 4),
    ADC_FRAME_STATUS_CHANNEL_ID_ERROR = (1U << 5),
    ADC_FRAME_STATUS_OVERRUN = (1U << 6),
    ADC_FRAME_STATUS_READ_ERROR = (1U << 7),
} adc_frame_status_flag_t;

typedef uint32_t adc_frame_status_flags_t;

/**
 * @brief Driver-independent status for one simultaneous ADC conversion.
 *
 * affected_channel_mask identifies channels implicated by channel-specific
 * status or integrity information. A zero mask means no channel was singled
 * out, for example for a whole-frame read error.
 */
typedef struct {
    adc_frame_status_flags_t flags;
    uint8_t affected_channel_mask;
} adc_frame_status_t;

/** @brief Scientific-validity and transient conditions for an ADC frame. */
typedef enum {
    ADC_SAMPLE_FLAG_NORMAL = 0,
    ADC_SAMPLE_FLAG_PULSE_ACTIVE = (1U << 0),
    ADC_SAMPLE_FLAG_SETTLING = (1U << 1),
    ADC_SAMPLE_FLAG_CONFIGURATION_CHANGE = (1U << 2),
    ADC_SAMPLE_FLAG_INVALID = (1U << 3),
} adc_sample_flag_t;

typedef uint32_t adc_sample_flags_t;

/**
 * @brief One indivisible, simultaneous eight-channel ADC conversion.
 *
 * Samples are signed, sign-extended 24-bit ADC codes. All eight positions
 * remain present; channel_mask and valid_mask determine which are meaningful.
 * This in-memory structure is not a wire or storage layout and must be packed
 * explicitly by a serializer.
 */
typedef struct {
    adc_sequence_t sequence;
    fw_monotonic_us_t timestamp_us;
    uint8_t channel_mask;
    uint8_t valid_mask;
    int32_t samples[ADC_FRAME_CHANNEL_COUNT];
    adc_frame_status_t adc_status;
    adc_sample_flags_t sample_flags;
    uint64_t dropped_before;
} adc_frame_t;

/** @brief Fixed-capacity ownership unit exchanged by acquisition consumers. */
typedef struct {
    uint32_t frame_count;
    uint8_t channel_mask;
    adc_sequence_t first_sequence;
    adc_frame_t frames[ADC_BLOCK_FRAME_CAPACITY];
} adc_block_t;

/** @brief Physical magnetic-card slot addressed by a pulse request. */
typedef enum {
    PULSE_CARD_SLOT_INVALID = 0,
    PULSE_CARD_SLOT_1,
    PULSE_CARD_SLOT_2,
} pulse_card_slot_t;

/** @brief Explicit on-demand magnetic-card operation. */
typedef enum {
    PULSE_OPERATION_INVALID = 0,
    PULSE_OPERATION_SET,
    PULSE_OPERATION_RESET,
    PULSE_OPERATION_SET_THEN_RESET_DIAGNOSTIC,
} pulse_operation_t;

typedef uint32_t pulse_request_id_t;

/** @brief Pulse command accepted for serialization by task_acquisition. */
typedef struct {
    pulse_request_id_t request_id;
    pulse_card_slot_t card_slot;
    pulse_operation_t operation;
    fw_monotonic_us_t requested_timestamp_us;
} pulse_request_t;

/** @brief Completed pulse command, including timing and affected ADC range. */
typedef struct {
    pulse_request_t request;
    uint32_t configured_control_width_us;
    fw_monotonic_us_t actual_timestamp_us;
    fw_monotonic_us_t settling_end_timestamp_us;
    adc_sequence_t first_affected_sequence;
    adc_sequence_t last_affected_sequence;
    bool affected_sequence_range_valid;
    fw_error_context_t result;
} pulse_result_t;

/**
 * @brief Acquisition counters copied into status snapshots and reports.
 *
 * Counters are monotonic for one boot. Overflow counters count overflow
 * incidents; dropped_frames counts the conversion frames lost as a result.
 */
typedef struct {
    uint64_t conversion_attempts;
    uint64_t completed_frames;
    uint64_t invalid_frames;
    uint64_t dropped_frames;
    uint64_t adc_overruns;
    uint64_t drdy_timestamp_ring_overflows;
    uint64_t block_pool_exhaustions;
    uint64_t ready_block_queue_overflows;
    uint64_t adc_read_errors;
    uint64_t adc_header_errors;
    uint64_t adc_crc_errors;
} acquisition_counters_t;

#endif /* GEOPHYS_TASK_ACQUISITION_H */
