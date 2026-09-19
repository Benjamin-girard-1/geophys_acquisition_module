#include "task_acquisition.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "adc_record.h"
#include "ad7779.h"
#include "board.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform_crc.h"
#include "platform_time.h"
#include "task_storage.h"

#define TASK_ACQUISITION_STACK_SIZE_BYTES UINT32_C(8192)
#define TASK_ACQUISITION_PRIORITY (tskIDLE_PRIORITY + 4U)
#define TASK_ACQUISITION_COMMAND_QUEUE_LENGTH UINT8_C(8)
#define TASK_ACQUISITION_RESPONSE_QUEUE_LENGTH UINT8_C(8)
#define TASK_ACQUISITION_COMMAND_TIMEOUT_MS UINT32_C(10000)
#define TASK_ACQUISITION_DRDY_RING_SIZE UINT8_C(64)
#define TASK_ACQUISITION_DRDY_BATCH_SIZE UINT8_C(16)
#define TASK_ACQUISITION_LIVE_RECORD_COUNT UINT8_C(16)

typedef enum {
    ACQUISITION_COMMAND_START = 0,
    ACQUISITION_COMMAND_STOP,
    ACQUISITION_COMMAND_STREAMING_START,
    ACQUISITION_COMMAND_STREAMING_STOP,
} acquisition_command_operation_t;

typedef struct {
    uint32_t identifier;
    acquisition_command_operation_t operation;
    task_acquisition_recording_config_t config;
    uint8_t streaming_decimation;
    uint8_t streaming_channel_mask;
} acquisition_command_t;

typedef struct {
    uint32_t identifier;
    fw_status_t status;
    fw_error_context_t error;
} acquisition_response_t;

typedef struct {
    fw_monotonic_100ns_t timestamp;
    adc_sequence_t sequence;
} drdy_event_t;

typedef struct {
    ad7779_t adc;
    task_acquisition_recording_config_t config;
    adc_record_builder_t storage_builder;
    uint8_t *current_storage_record;
    adc_record_builder_t streaming_builder;
    uint8_t *current_streaming_record;
    uint8_t streaming_records[TASK_ACQUISITION_LIVE_RECORD_COUNT]
                             [ADC_RECORD_SIZE_BYTES];
    QueueHandle_t streaming_free_records;
    QueueHandle_t streaming_ready_records;
    QueueHandle_t commands;
    QueueHandle_t responses;
    SemaphoreHandle_t command_mutex;
    TaskHandle_t task_handle;
    drdy_event_t drdy_events[TASK_ACQUISITION_DRDY_RING_SIZE];
    volatile uint32_t drdy_head;
    volatile uint32_t drdy_tail;
    volatile uint32_t isr_overflows;
    volatile adc_sequence_t isr_next_sequence;
    acquisition_counters_t counters;
    uint32_t storage_payload_number;
    uint32_t streaming_payload_number;
    uint32_t observed_isr_overflows;
    adc_record_status_t storage_pending_status;
    adc_record_status_t streaming_pending_status;
    uint8_t streaming_decimation;
    uint8_t streaming_channel_mask;
    bool adc_initialized;
    bool drdy_attached;
    volatile bool active;
    bool recording_active;
    volatile bool streaming_active;
    bool initialized;
} task_acquisition_state_t;

static task_acquisition_state_t s_acquisition;
static portMUX_TYPE s_acquisition_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_next_command_identifier;

static void clear_error(fw_error_context_t *error)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = FW_STATUS_OK,
            .resource = FW_ERROR_RESOURCE_NONE,
            .operation = FW_ERROR_OPERATION_NONE,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = 0U,
        };
    }
}

static fw_status_t set_error(fw_error_context_t *error,
                             fw_status_t status,
                             fw_error_operation_t operation,
                             uint32_t detail)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_ADC,
            .operation = operation,
            .instance = 0U,
            .detail = detail,
        };
    }
    return status;
}

static uint32_t record_crc32(void *context,
                             const uint8_t *data,
                             size_t length_bytes)
{
    (void)context;
    return platform_crc32_le(UINT32_C(0xFFFFFFFF), data, length_bytes) ^
           UINT32_C(0xFFFFFFFF);
}

static bool recording_config_is_valid(
    const task_acquisition_recording_config_t *config)
{
    if (config == NULL ||
        (config->channel_mask != UINT8_C(0x0F) &&
         config->channel_mask != UINT8_C(0xF0) &&
         config->channel_mask != UINT8_C(0xFF))) {
        return false;
    }
    switch (config->sample_rate_sps) {
    case UINT32_C(500):
    case UINT32_C(1000):
    case UINT32_C(2000):
    case UINT32_C(4000):
    case UINT32_C(8000):
    case UINT32_C(16000):
        break;
    default:
        return false;
    }
    for (size_t channel = 0U; channel < ADC_FRAME_CHANNEL_COUNT; channel++) {
        const uint8_t gain = config->gains[channel];
        if (gain != 1U && gain != 2U && gain != 4U && gain != 8U) {
            return false;
        }
    }
    return true;
}

static uint16_t pack_gains(
    const task_acquisition_recording_config_t *config)
{
    uint16_t packed = 0U;
    for (uint8_t channel = 0U; channel < ADC_FRAME_CHANNEL_COUNT; channel++) {
        uint16_t encoded = 0U;
        switch (config->gains[channel]) {
        case 2U:
            encoded = 1U;
            break;
        case 4U:
            encoded = 2U;
            break;
        case 8U:
            encoded = 3U;
            break;
        case 1U:
        default:
            encoded = 0U;
            break;
        }
        packed |= (uint16_t)(encoded << (2U * channel));
    }
    return packed;
}

static int status_priority(adc_record_status_t status)
{
    switch (status) {
    case ADC_RECORD_STATUS_CRITICAL_ERROR:
        return 3;
    case ADC_RECORD_STATUS_TIMING_ERROR:
        return 2;
    case ADC_RECORD_STATUS_CONVERSION_ERROR:
        return 1;
    case ADC_RECORD_STATUS_OK:
    default:
        return 0;
    }
}

static void raise_storage_pending_status(adc_record_status_t status)
{
    if (status_priority(status) >
        status_priority(s_acquisition.storage_pending_status)) {
        s_acquisition.storage_pending_status = status;
    }
}

static void update_storage_builder_status(adc_record_status_t status)
{
    if (s_acquisition.current_storage_record == NULL) {
        raise_storage_pending_status(status);
        return;
    }
    if (status_priority(status) >
        status_priority(s_acquisition.storage_builder.metadata.status)) {
        (void)adc_record_builder_set_status(
            &s_acquisition.storage_builder, status);
    }
}

static void raise_streaming_pending_status(adc_record_status_t status)
{
    if (status_priority(status) >
        status_priority(s_acquisition.streaming_pending_status)) {
        s_acquisition.streaming_pending_status = status;
    }
}

static void update_streaming_builder_status(adc_record_status_t status)
{
    if (s_acquisition.current_streaming_record == NULL) {
        raise_streaming_pending_status(status);
        return;
    }
    if (status_priority(status) >
        status_priority(s_acquisition.streaming_builder.metadata.status)) {
        (void)adc_record_builder_set_status(
            &s_acquisition.streaming_builder, status);
    }
}

static void IRAM_ATTR adc_drdy_isr(void *context)
{
    (void)context;
    fw_monotonic_100ns_t timestamp;
    if (!platform_monotonic_time_100ns_isr(&timestamp)) {
        return;
    }

    bool queued = false;
    portENTER_CRITICAL_ISR(&s_acquisition_lock);
    const adc_sequence_t sequence = s_acquisition.isr_next_sequence++;
    const uint32_t head = s_acquisition.drdy_head;
    const uint32_t next =
        (head + 1U) % TASK_ACQUISITION_DRDY_RING_SIZE;
    if (next == s_acquisition.drdy_tail) {
        s_acquisition.isr_overflows++;
    } else {
        s_acquisition.drdy_events[head] = (drdy_event_t) {
            .timestamp = timestamp,
            .sequence = sequence,
        };
        s_acquisition.drdy_head = next;
        queued = true;
    }
    portEXIT_CRITICAL_ISR(&s_acquisition_lock);

    if (queued && s_acquisition.task_handle != NULL) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_acquisition.task_handle,
                               &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

static bool pop_drdy_event(drdy_event_t *event)
{
    bool available = false;
    portENTER_CRITICAL(&s_acquisition_lock);
    const uint32_t tail = s_acquisition.drdy_tail;
    if (tail != s_acquisition.drdy_head) {
        *event = s_acquisition.drdy_events[tail];
        s_acquisition.drdy_tail =
            (tail + 1U) % TASK_ACQUISITION_DRDY_RING_SIZE;
        available = true;
    }
    portEXIT_CRITICAL(&s_acquisition_lock);
    return available;
}

static bool drdy_event_pending(void)
{
    bool pending;
    portENTER_CRITICAL(&s_acquisition_lock);
    pending = s_acquisition.drdy_tail != s_acquisition.drdy_head;
    portEXIT_CRITICAL(&s_acquisition_lock);
    return pending;
}

static uint32_t take_isr_overflow_delta(void)
{
    uint32_t total;
    portENTER_CRITICAL(&s_acquisition_lock);
    total = s_acquisition.isr_overflows;
    portEXIT_CRITICAL(&s_acquisition_lock);
    const uint32_t delta = total - s_acquisition.observed_isr_overflows;
    s_acquisition.observed_isr_overflows = total;
    return delta;
}

static fw_status_t begin_storage_record_if_needed(fw_error_context_t *error)
{
    if (s_acquisition.current_storage_record != NULL) {
        return FW_STATUS_OK;
    }
    uint8_t *record = NULL;
    fw_status_t status = task_storage_record_acquire(&record, error);
    if (status != FW_STATUS_OK) {
        s_acquisition.counters.block_pool_exhaustions++;
        return status;
    }
    const adc_record_metadata_t metadata = {
        .channel_mask = s_acquisition.config.channel_mask,
        .status = s_acquisition.storage_pending_status,
        .packed_gain = pack_gains(&s_acquisition.config),
        .payload_number = s_acquisition.storage_payload_number,
        .first_conversion_sequence = 0U,
        .first_monotonic_timestamp_100ns = 0U,
        .sample_period_100ns =
            UINT32_C(10000000) / s_acquisition.config.sample_rate_sps,
    };
    const adc_record_codec_status_t codec_status =
        adc_record_builder_begin(
            &s_acquisition.storage_builder, record, &metadata);
    if (codec_status != ADC_RECORD_CODEC_OK) {
        task_storage_record_release(record);
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_WRITE,
                         (uint32_t)codec_status);
    }
    s_acquisition.current_storage_record = record;
    s_acquisition.storage_pending_status = ADC_RECORD_STATUS_OK;
    return FW_STATUS_OK;
}

static void finish_storage_record_if_complete(void)
{
    if (s_acquisition.current_storage_record == NULL ||
        s_acquisition.storage_builder.appended_conversions !=
        s_acquisition.storage_builder.required_conversions) {
        return;
    }
    const uint8_t conversions =
        s_acquisition.storage_builder.required_conversions;
    const adc_record_codec_status_t codec_status =
        adc_record_builder_finalize(&s_acquisition.storage_builder,
                                    record_crc32, NULL);
    s_acquisition.storage_payload_number++;
    if (codec_status != ADC_RECORD_CODEC_OK ||
        task_storage_record_submit(s_acquisition.current_storage_record,
                                   NULL) != FW_STATUS_OK) {
        s_acquisition.counters.ready_block_queue_overflows++;
        s_acquisition.counters.dropped_frames += conversions;
        raise_storage_pending_status(ADC_RECORD_STATUS_TIMING_ERROR);
        if (codec_status != ADC_RECORD_CODEC_OK) {
            task_storage_record_release(
                s_acquisition.current_storage_record);
        }
    }
    s_acquisition.current_storage_record = NULL;
    memset(&s_acquisition.storage_builder, 0,
           sizeof(s_acquisition.storage_builder));
}

static fw_status_t begin_streaming_record_if_needed(void)
{
    if (s_acquisition.current_streaming_record != NULL) {
        return FW_STATUS_OK;
    }
    uint8_t *record = NULL;
    if (xQueueReceive(s_acquisition.streaming_free_records,
                      &record, 0U) != pdTRUE) {
        s_acquisition.counters.block_pool_exhaustions++;
        return FW_STATUS_OVERFLOW;
    }
    const uint8_t factor =
        (s_acquisition.streaming_decimation == 0U) ?
        1U : s_acquisition.streaming_decimation;
    const adc_record_metadata_t metadata = {
        .channel_mask = s_acquisition.streaming_channel_mask,
        .status = s_acquisition.streaming_pending_status,
        .packed_gain = pack_gains(&s_acquisition.config),
        .payload_number = s_acquisition.streaming_payload_number,
        .first_conversion_sequence = 0U,
        .first_monotonic_timestamp_100ns = 0U,
        .sample_period_100ns =
            (UINT32_C(10000000) / s_acquisition.config.sample_rate_sps) *
            factor,
    };
    const adc_record_codec_status_t codec_status =
        adc_record_builder_begin(
            &s_acquisition.streaming_builder, record, &metadata);
    if (codec_status != ADC_RECORD_CODEC_OK) {
        (void)xQueueSend(s_acquisition.streaming_free_records,
                         &record, 0U);
        return FW_STATUS_INTERNAL;
    }
    s_acquisition.current_streaming_record = record;
    s_acquisition.streaming_pending_status = ADC_RECORD_STATUS_OK;
    return FW_STATUS_OK;
}

static void finish_streaming_record_if_complete(void)
{
    if (s_acquisition.current_streaming_record == NULL ||
        s_acquisition.streaming_builder.appended_conversions !=
        s_acquisition.streaming_builder.required_conversions) {
        return;
    }
    const uint8_t conversions =
        s_acquisition.streaming_builder.required_conversions;
    const adc_record_codec_status_t codec_status =
        adc_record_builder_finalize(&s_acquisition.streaming_builder,
                                    record_crc32, NULL);
    s_acquisition.streaming_payload_number++;
    if (codec_status != ADC_RECORD_CODEC_OK ||
        xQueueSend(s_acquisition.streaming_ready_records,
                   &s_acquisition.current_streaming_record, 0U) != pdTRUE) {
        s_acquisition.counters.ready_block_queue_overflows++;
        s_acquisition.counters.dropped_frames += conversions;
        raise_streaming_pending_status(ADC_RECORD_STATUS_TIMING_ERROR);
        (void)xQueueSend(s_acquisition.streaming_free_records,
                         &s_acquisition.current_streaming_record, 0U);
    }
    s_acquisition.current_streaming_record = NULL;
    memset(&s_acquisition.streaming_builder, 0,
           sizeof(s_acquisition.streaming_builder));
}

static void account_isr_overflow(void)
{
    const uint32_t missed = take_isr_overflow_delta();
    if (missed == 0U) {
        return;
    }
    s_acquisition.counters.drdy_timestamp_ring_overflows += missed;
    s_acquisition.counters.adc_overruns += missed;
    s_acquisition.counters.conversion_attempts += missed;
    s_acquisition.counters.dropped_frames += missed;
    update_storage_builder_status(ADC_RECORD_STATUS_TIMING_ERROR);
    update_streaming_builder_status(ADC_RECORD_STATUS_TIMING_ERROR);
}

static adc_record_status_t validate_and_decode_frame(
    const uint8_t raw_frame[AD7779_RAW_FRAME_BYTES],
    int32_t samples[AD7779_CHANNEL_COUNT])
{
    ad7779_frame_validation_t validation;
    const fw_status_t validation_status = ad7779_validate_frame(
        raw_frame, AD7779_RAW_FRAME_BYTES, AD7779_FRAME_HEADER_CRC,
        &validation, NULL);
    const fw_status_t decode_status = ad7779_decode_frame(
        raw_frame, AD7779_RAW_FRAME_BYTES, samples, NULL);
    if (decode_status != FW_STATUS_OK) {
        memset(samples, 0, sizeof(int32_t) * AD7779_CHANNEL_COUNT);
        s_acquisition.counters.adc_read_errors++;
        return ADC_RECORD_STATUS_CONVERSION_ERROR;
    }
    if ((validation.faults & AD7779_FAULT_CHANNEL_ID) != 0U) {
        s_acquisition.counters.adc_header_errors++;
    }
    if ((validation.faults & AD7779_FAULT_DATA_CRC) != 0U) {
        s_acquisition.counters.adc_crc_errors++;
    }
    if (validation_status == FW_STATUS_HARDWARE_FAULT) {
        return ADC_RECORD_STATUS_CRITICAL_ERROR;
    }
    if (validation_status != FW_STATUS_OK) {
        return ADC_RECORD_STATUS_CONVERSION_ERROR;
    }
    return ADC_RECORD_STATUS_OK;
}

static void process_storage_conversion(
    const drdy_event_t *event,
    const int32_t samples[AD7779_CHANNEL_COUNT],
    adc_record_status_t frame_status)
{
    if (!s_acquisition.recording_active ||
        !task_storage_recording_active()) {
        return;
    }
    if (begin_storage_record_if_needed(NULL) != FW_STATUS_OK) {
        s_acquisition.counters.dropped_frames++;
        raise_storage_pending_status(ADC_RECORD_STATUS_TIMING_ERROR);
        return;
    }
    update_storage_builder_status(frame_status);
    const adc_record_codec_status_t append_status =
        adc_record_builder_append(&s_acquisition.storage_builder,
                                  (uint32_t)event->sequence,
                                  event->timestamp, samples);
    if (append_status != ADC_RECORD_CODEC_OK) {
        task_storage_record_release(s_acquisition.current_storage_record);
        s_acquisition.current_storage_record = NULL;
        memset(&s_acquisition.storage_builder, 0,
               sizeof(s_acquisition.storage_builder));
        s_acquisition.counters.dropped_frames++;
        raise_storage_pending_status(ADC_RECORD_STATUS_TIMING_ERROR);
        return;
    }
    finish_storage_record_if_complete();
}

static void process_streaming_conversion(
    const drdy_event_t *event,
    const int32_t samples[AD7779_CHANNEL_COUNT],
    adc_record_status_t frame_status)
{
    if (!s_acquisition.streaming_active) {
        return;
    }
    const uint8_t factor =
        (s_acquisition.streaming_decimation == 0U) ?
        1U : s_acquisition.streaming_decimation;
    if ((event->sequence % factor) != 0U) {
        return;
    }
    if (begin_streaming_record_if_needed() != FW_STATUS_OK) {
        s_acquisition.counters.dropped_frames++;
        raise_streaming_pending_status(ADC_RECORD_STATUS_TIMING_ERROR);
        return;
    }
    update_streaming_builder_status(frame_status);
    const adc_record_codec_status_t append_status =
        adc_record_builder_append(&s_acquisition.streaming_builder,
                                  (uint32_t)event->sequence,
                                  event->timestamp, samples);
    if (append_status != ADC_RECORD_CODEC_OK) {
        (void)xQueueSend(s_acquisition.streaming_free_records,
                         &s_acquisition.current_streaming_record, 0U);
        s_acquisition.current_streaming_record = NULL;
        memset(&s_acquisition.streaming_builder, 0,
               sizeof(s_acquisition.streaming_builder));
        s_acquisition.counters.dropped_frames++;
        raise_streaming_pending_status(ADC_RECORD_STATUS_TIMING_ERROR);
        return;
    }
    finish_streaming_record_if_complete();
}

static void process_conversion(const drdy_event_t *event)
{
    s_acquisition.counters.conversion_attempts++;

    uint8_t raw_frame[AD7779_RAW_FRAME_BYTES];
    int32_t samples[AD7779_CHANNEL_COUNT] = {0};
    adc_record_status_t frame_status = ADC_RECORD_STATUS_OK;
    if (ad7779_read_frame(&s_acquisition.adc, raw_frame, NULL) !=
        FW_STATUS_OK) {
        s_acquisition.counters.adc_read_errors++;
        memset(samples, 0, sizeof(samples));
        frame_status = ADC_RECORD_STATUS_CONVERSION_ERROR;
    } else {
        frame_status = validate_and_decode_frame(raw_frame, samples);
    }
    if (frame_status != ADC_RECORD_STATUS_OK) {
        s_acquisition.counters.invalid_frames++;
    } else {
        s_acquisition.counters.completed_frames++;
    }

    process_storage_conversion(event, samples, frame_status);
    process_streaming_conversion(event, samples, frame_status);
}

static void discard_streaming_records(void)
{
    if (s_acquisition.current_streaming_record != NULL) {
        (void)xQueueSend(s_acquisition.streaming_free_records,
                         &s_acquisition.current_streaming_record, 0U);
        s_acquisition.current_streaming_record = NULL;
    }
    uint8_t *record = NULL;
    while (xQueueReceive(s_acquisition.streaming_ready_records,
                         &record, 0U) == pdTRUE) {
        (void)xQueueSend(s_acquisition.streaming_free_records,
                         &record, 0U);
    }
    memset(&s_acquisition.streaming_builder, 0,
           sizeof(s_acquisition.streaming_builder));
}

static void discard_partial_storage_record(void)
{
    if (s_acquisition.current_storage_record != NULL) {
        task_storage_record_release(s_acquisition.current_storage_record);
        s_acquisition.current_storage_record = NULL;
    }
    memset(&s_acquisition.storage_builder, 0,
           sizeof(s_acquisition.storage_builder));
}

static void discard_ring_and_partial_records(void)
{
    portENTER_CRITICAL(&s_acquisition_lock);
    s_acquisition.drdy_tail = s_acquisition.drdy_head;
    portEXIT_CRITICAL(&s_acquisition_lock);
    discard_partial_storage_record();
    discard_streaming_records();
}

static fw_status_t configure_adc(
    const task_acquisition_recording_config_t *config,
    fw_error_context_t *error)
{
    ad7779_channel_config_t channels = {
        .enabled_mask = config->channel_mask,
    };
    for (size_t channel = 0U; channel < AD7779_CHANNEL_COUNT; channel++) {
        channels.gains[channel] = (ad7779_gain_t)config->gains[channel];
    }
    fw_status_t status = ad7779_configure_channels(
        &s_acquisition.adc, &channels, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    return ad7779_configure_output_rate(
        &s_acquisition.adc, (ad7779_output_rate_t)config->sample_rate_sps,
        error);
}

static void acquisition_cleanup_best_effort(void)
{
    if (s_acquisition.drdy_attached) {
        (void)board_adc_drdy_disable(NULL);
        (void)board_adc_drdy_detach(NULL);
        s_acquisition.drdy_attached = false;
    }
    discard_ring_and_partial_records();
    if (s_acquisition.adc_initialized) {
        (void)board_adc_deinitialize(&s_acquisition.adc, NULL);
        s_acquisition.adc_initialized = false;
    }
    (void)board_adc_power_down(NULL);
    s_acquisition.active = false;
    s_acquisition.recording_active = false;
    s_acquisition.streaming_active = false;
}

static fw_status_t start_acquisition(
    const task_acquisition_recording_config_t *config,
    fw_error_context_t *error)
{
    if (!recording_config_is_valid(config)) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_ENABLE, 0U);
    }
    if (s_acquisition.active && s_acquisition.streaming_active) {
        discard_partial_storage_record();
        s_acquisition.storage_payload_number = 0U;
        s_acquisition.storage_pending_status = ADC_RECORD_STATUS_OK;
        s_acquisition.recording_active = true;
        clear_error(error);
        return FW_STATUS_OK;
    }
    if (s_acquisition.active) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_ENABLE, 0U);
    }

    fw_status_t status = board_adc_power_up(error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = board_adc_initialize(&s_acquisition.adc, error);
    if (status != FW_STATUS_OK) {
        acquisition_cleanup_best_effort();
        return status;
    }
    s_acquisition.adc_initialized = true;
    status = configure_adc(config, error);
    if (status != FW_STATUS_OK) {
        acquisition_cleanup_best_effort();
        return status;
    }
    status = board_adc_drdy_attach(adc_drdy_isr, NULL, error);
    if (status != FW_STATUS_OK) {
        acquisition_cleanup_best_effort();
        return status;
    }
    s_acquisition.drdy_attached = true;

    s_acquisition.config = *config;
    s_acquisition.recording_active = task_storage_recording_active();
    s_acquisition.storage_payload_number = 0U;
    s_acquisition.storage_pending_status = ADC_RECORD_STATUS_OK;
    discard_ring_and_partial_records();
    portENTER_CRITICAL(&s_acquisition_lock);
    s_acquisition.isr_next_sequence = 0U;
    s_acquisition.observed_isr_overflows = s_acquisition.isr_overflows;
    portEXIT_CRITICAL(&s_acquisition_lock);
    status = ad7779_start(&s_acquisition.adc, error);
    if (status != FW_STATUS_OK) {
        acquisition_cleanup_best_effort();
        return status;
    }
    status = board_adc_drdy_enable(error);
    if (status != FW_STATUS_OK) {
        acquisition_cleanup_best_effort();
        return status;
    }
    s_acquisition.active = true;
    return FW_STATUS_OK;
}

static fw_status_t stop_acquisition(fw_error_context_t *error)
{
    if (!s_acquisition.active) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_DISABLE, 0U);
    }
    clear_error(error);
    fw_status_t first_status = board_adc_drdy_disable(error);
    fw_error_context_t first_error;
    if (error != NULL) {
        first_error = *error;
    } else {
        clear_error(&first_error);
    }
    s_acquisition.active = false;
    s_acquisition.recording_active = false;
    s_acquisition.streaming_active = false;
    discard_ring_and_partial_records();

    fw_error_context_t current_error;
    clear_error(&current_error);
    fw_status_t status = board_adc_drdy_detach(&current_error);
    s_acquisition.drdy_attached = false;
    if (first_status == FW_STATUS_OK && status != FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }
    clear_error(&current_error);
    status = board_adc_deinitialize(&s_acquisition.adc, &current_error);
    s_acquisition.adc_initialized = false;
    if (first_status == FW_STATUS_OK && status != FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }
    clear_error(&current_error);
    status = board_adc_power_down(&current_error);
    if (first_status == FW_STATUS_OK && status != FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }
    if (first_status != FW_STATUS_OK && error != NULL) {
        *error = first_error;
    }
    return first_status;
}

static bool streaming_decimation_is_valid(uint8_t decimation)
{
    return decimation == 0U || decimation == 2U || decimation == 4U ||
           decimation == 5U || decimation == 10U || decimation == 20U;
}

static fw_status_t start_streaming(uint8_t decimation,
                                   uint8_t channel_mask,
                                   fw_error_context_t *error)
{
    if (!streaming_decimation_is_valid(decimation) ||
        (channel_mask != UINT8_C(0x0F) &&
         channel_mask != UINT8_C(0xF0) &&
         channel_mask != UINT8_C(0xFF))) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_ENABLE, channel_mask);
    }
    if (!s_acquisition.active || s_acquisition.streaming_active) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_ENABLE, 0U);
    }
    if ((channel_mask & s_acquisition.config.channel_mask) != channel_mask) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_ENABLE, channel_mask);
    }

    discard_streaming_records();
    s_acquisition.streaming_decimation = decimation;
    s_acquisition.streaming_channel_mask = channel_mask;
    s_acquisition.streaming_payload_number = 0U;
    s_acquisition.streaming_pending_status = ADC_RECORD_STATUS_OK;
    s_acquisition.streaming_active = true;
    clear_error(error);
    return FW_STATUS_OK;
}

static fw_status_t stop_streaming(fw_error_context_t *error)
{
    if (!s_acquisition.streaming_active) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_DISABLE, 0U);
    }
    s_acquisition.streaming_active = false;
    discard_streaming_records();
    clear_error(error);
    return FW_STATUS_OK;
}

static void handle_command(const acquisition_command_t *command,
                           acquisition_response_t *response)
{
    memset(response, 0, sizeof(*response));
    response->identifier = command->identifier;
    clear_error(&response->error);
    switch (command->operation) {
    case ACQUISITION_COMMAND_START:
        response->status = start_acquisition(
            &command->config, &response->error);
        break;
    case ACQUISITION_COMMAND_STOP:
        response->status = stop_acquisition(&response->error);
        break;
    case ACQUISITION_COMMAND_STREAMING_START:
        response->status = start_streaming(
            command->streaming_decimation,
            command->streaming_channel_mask,
            &response->error);
        break;
    case ACQUISITION_COMMAND_STREAMING_STOP:
        response->status = stop_streaming(&response->error);
        break;
    default:
        response->status = set_error(
            &response->error, FW_STATUS_INTERNAL,
            FW_ERROR_OPERATION_NONE, (uint32_t)command->operation);
        break;
    }
}

static void acquisition_task_run(void *context)
{
    (void)context;
    for (;;) {
        acquisition_command_t command;
        if (!s_acquisition.active) {
            if (xQueueReceive(s_acquisition.commands, &command,
                              portMAX_DELAY) != pdTRUE) {
                continue;
            }
            acquisition_response_t response;
            handle_command(&command, &response);
            (void)xQueueSend(s_acquisition.responses, &response,
                             portMAX_DELAY);
            continue;
        }

        /*
         * DRDY and commands both notify this task. Blocking here avoids the
         * former 1 ms queue poll, which rounded to zero ticks at the configured
         * 100 Hz FreeRTOS tick rate and starved communication and storage.
         */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (xQueueReceive(s_acquisition.commands, &command, 0U) == pdTRUE) {
            acquisition_response_t response;
            handle_command(&command, &response);
            (void)xQueueSend(s_acquisition.responses, &response,
                             portMAX_DELAY);
            continue;
        }

        drdy_event_t event;
        uint8_t processed = 0U;
        while (s_acquisition.active &&
               processed < TASK_ACQUISITION_DRDY_BATCH_SIZE &&
               pop_drdy_event(&event)) {
            process_conversion(&event);
            processed++;
        }
        account_isr_overflow();

        /*
         * A sustained backlog means the acquisition path cannot keep up with
         * DRDY. Bound command latency and leave a 1 ms window for the UART and
         * storage control planes. The ISR keeps sequencing conversions and the
         * existing overflow counters/status marking expose any resulting loss.
         */
        if (s_acquisition.active && drdy_event_pending()) {
            vTaskDelay(1U);
        }
    }
}

fw_status_t task_acquisition_initialize(fw_error_context_t *error)
{
    clear_error(error);
    if (s_acquisition.initialized) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    memset(&s_acquisition, 0, sizeof(s_acquisition));
    s_acquisition.commands = xQueueCreate(
        TASK_ACQUISITION_COMMAND_QUEUE_LENGTH,
        sizeof(acquisition_command_t));
    s_acquisition.responses = xQueueCreate(
        TASK_ACQUISITION_RESPONSE_QUEUE_LENGTH,
        sizeof(acquisition_response_t));
    s_acquisition.streaming_free_records = xQueueCreate(
        TASK_ACQUISITION_LIVE_RECORD_COUNT, sizeof(uint8_t *));
    s_acquisition.streaming_ready_records = xQueueCreate(
        TASK_ACQUISITION_LIVE_RECORD_COUNT, sizeof(uint8_t *));
    s_acquisition.command_mutex = xSemaphoreCreateMutex();
    if (s_acquisition.commands == NULL || s_acquisition.responses == NULL ||
        s_acquisition.streaming_free_records == NULL ||
        s_acquisition.streaming_ready_records == NULL ||
        s_acquisition.command_mutex == NULL) {
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    for (size_t index = 0U;
         index < TASK_ACQUISITION_LIVE_RECORD_COUNT;
         index++) {
        uint8_t *record = s_acquisition.streaming_records[index];
        if (xQueueSend(s_acquisition.streaming_free_records,
                       &record, 0U) != pdTRUE) {
            return set_error(error, FW_STATUS_INTERNAL,
                             FW_ERROR_OPERATION_INITIALIZE,
                             (uint32_t)index);
        }
    }
    if (xTaskCreate(acquisition_task_run, "acquisition",
                    TASK_ACQUISITION_STACK_SIZE_BYTES, NULL,
                    TASK_ACQUISITION_PRIORITY,
                    &s_acquisition.task_handle) != pdPASS) {
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE,
                         TASK_ACQUISITION_STACK_SIZE_BYTES);
    }
    s_acquisition.initialized = true;
    return FW_STATUS_OK;
}

static fw_status_t execute_command(acquisition_command_t *command,
                                   fw_error_context_t *error)
{
    clear_error(error);
    if (!s_acquisition.initialized || command == NULL) {
        return set_error(error,
                         s_acquisition.initialized ?
                         FW_STATUS_INVALID_ARGUMENT :
                         FW_STATUS_NOT_INITIALIZED,
                         FW_ERROR_OPERATION_WAIT, 0U);
    }
    const TickType_t timeout =
        pdMS_TO_TICKS(TASK_ACQUISITION_COMMAND_TIMEOUT_MS);
    if (xSemaphoreTake(s_acquisition.command_mutex, timeout) != pdTRUE) {
        return set_error(error, FW_STATUS_BUSY,
                         FW_ERROR_OPERATION_WAIT, 0U);
    }
    command->identifier = ++s_next_command_identifier;
    if (xQueueSend(s_acquisition.commands, command, timeout) != pdTRUE) {
        (void)xSemaphoreGive(s_acquisition.command_mutex);
        return set_error(error, FW_STATUS_TIMEOUT, FW_ERROR_OPERATION_WAIT,
                         command->identifier);
    }
    /* Wake an active acquisition task that is blocked on its DRDY notification. */
    xTaskNotifyGive(s_acquisition.task_handle);
    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        acquisition_response_t response;
        const TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout ||
            xQueueReceive(s_acquisition.responses, &response,
                          timeout - elapsed) != pdTRUE) {
            (void)xSemaphoreGive(s_acquisition.command_mutex);
            return set_error(error, FW_STATUS_TIMEOUT,
                             FW_ERROR_OPERATION_WAIT,
                             command->identifier);
        }
        if (response.identifier == command->identifier) {
            if (response.status != FW_STATUS_OK && error != NULL) {
                *error = response.error;
            }
            (void)xSemaphoreGive(s_acquisition.command_mutex);
            return response.status;
        }
    }
}

fw_status_t task_acquisition_recording_start(
    const task_acquisition_recording_config_t *config,
    fw_error_context_t *error)
{
    if (config == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_ENABLE, 0U);
    }
    acquisition_command_t command = {
        .operation = ACQUISITION_COMMAND_START,
        .config = *config,
    };
    return execute_command(&command, error);
}

fw_status_t task_acquisition_recording_stop(fw_error_context_t *error)
{
    acquisition_command_t command = {
        .operation = ACQUISITION_COMMAND_STOP,
    };
    return execute_command(&command, error);
}

fw_status_t task_acquisition_streaming_start(
    uint8_t decimation,
    uint8_t channel_mask,
    fw_error_context_t *error)
{
    acquisition_command_t command = {
        .operation = ACQUISITION_COMMAND_STREAMING_START,
        .streaming_decimation = decimation,
        .streaming_channel_mask = channel_mask,
    };
    return execute_command(&command, error);
}

fw_status_t task_acquisition_streaming_stop(fw_error_context_t *error)
{
    acquisition_command_t command = {
        .operation = ACQUISITION_COMMAND_STREAMING_STOP,
    };
    return execute_command(&command, error);
}

fw_status_t task_acquisition_stream_record_take(
    uint8_t **record,
    fw_error_context_t *error)
{
    clear_error(error);
    if (record == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, 0U);
    }
    *record = NULL;
    if (!s_acquisition.initialized) {
        return set_error(error, FW_STATUS_NOT_INITIALIZED,
                         FW_ERROR_OPERATION_READ, 0U);
    }
    if (xQueueReceive(s_acquisition.streaming_ready_records,
                      record, 0U) != pdTRUE) {
        return FW_STATUS_NOT_FOUND;
    }
    return FW_STATUS_OK;
}

void task_acquisition_stream_record_release(uint8_t *record)
{
    if (record != NULL && s_acquisition.streaming_free_records != NULL) {
        (void)xQueueSend(s_acquisition.streaming_free_records,
                         &record, 0U);
    }
}

void task_acquisition_get_counters(acquisition_counters_t *counters)
{
    if (counters == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_acquisition_lock);
    *counters = s_acquisition.counters;
    portEXIT_CRITICAL(&s_acquisition_lock);
}

bool task_acquisition_is_active(void)
{
    return s_acquisition.active;
}

bool task_acquisition_streaming_is_active(void)
{
    return s_acquisition.streaming_active;
}
