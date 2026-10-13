#include "task_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "platform_crc.h"
#include "platform_device.h"
#include "platform_memory.h"
#include "platform_storage.h"
#include "protocol_messages.h"
#include "recording_header.h"

#define TASK_STORAGE_STACK_SIZE_BYTES UINT32_C(8192)
#define TASK_STORAGE_PRIORITY (tskIDLE_PRIORITY + 1U)
#define TASK_STORAGE_PSRAM_BUFFER_COUNT UINT8_C(3)
#define TASK_STORAGE_PSRAM_BUFFER_SIZE_BYTES (2U * 1024U * 1024U)
#define TASK_STORAGE_RECORDS_PER_PSRAM_BUFFER \
    (TASK_STORAGE_PSRAM_BUFFER_SIZE_BYTES / ADC_RECORD_SIZE_BYTES)
#define TASK_STORAGE_BATCH_BUFFER_COUNT UINT8_C(2)
#define TASK_STORAGE_BATCH_CAPACITY_RECORDS \
    (TASK_STORAGE_BATCH_BUFFER_COUNT * \
     TASK_STORAGE_RECORDS_PER_PSRAM_BUFFER)
#define TASK_STORAGE_FOUR_CHANNEL_1KSPS_BATCH_RECORDS UINT32_C(7500)
#define TASK_STORAGE_FOUR_CHANNEL_FAST_BATCH_RECORDS UINT32_C(6000)
#define TASK_STORAGE_INVALID_BUFFER_INDEX UINT8_MAX
#define TASK_STORAGE_COMMAND_QUEUE_LENGTH UINT8_C(4)
#define TASK_STORAGE_RESPONSE_QUEUE_LENGTH UINT8_C(4)
#define TASK_STORAGE_COMMAND_TIMEOUT_MS UINT32_C(10000)
#define TASK_STORAGE_RECORDING_DIRECTORY "/recordings"
#define TASK_STORAGE_RECORDING_PATH_SIZE_BYTES UINT8_C(48)

typedef enum {
    STORAGE_COMMAND_OPEN = 0,
    STORAGE_COMMAND_CLOSE,
    STORAGE_COMMAND_ABORT,
    STORAGE_COMMAND_COUNT,
    STORAGE_COMMAND_INFO,
    STORAGE_COMMAND_DELETE,
    STORAGE_COMMAND_READ,
} storage_command_operation_t;

typedef struct {
    uint32_t identifier;
    storage_command_operation_t operation;
    uint16_t index;
    uint32_t offset_bytes;
    task_storage_recording_config_t recording_config;
    char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES];
} storage_command_t;

typedef struct {
    uint32_t identifier;
    fw_status_t status;
    fw_error_context_t error;
    uint16_t count;
    char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES];
    task_storage_recording_info_t info;
    task_storage_recording_chunk_t chunk;
} storage_response_t;

typedef struct {
    char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES];
    uint32_t size_bytes;
} catalog_entry_t;

typedef enum {
    RECORD_BUFFER_FREE = 0,
    RECORD_BUFFER_FILLING,
    RECORD_BUFFER_SEALED,
    RECORD_BUFFER_READY,
    RECORD_BUFFER_WRITING,
} record_buffer_state_t;

typedef struct {
    uint8_t *data;
    uint32_t committed_records;
    uint32_t order;
    record_buffer_state_t state;
} record_buffer_t;

typedef struct {
    task_storage_config_t config;
    platform_storage_t *storage;
    platform_storage_file_t *active_file;
    QueueHandle_t commands;
    QueueHandle_t responses;
    TaskHandle_t task_handle;
    record_buffer_t record_buffers[TASK_STORAGE_PSRAM_BUFFER_COUNT];
    catalog_entry_t catalog[TASK_STORAGE_RECORDING_MAX_COUNT];
    uint16_t catalog_count;
    char active_name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES];
    uint32_t active_size_bytes;
    uint32_t next_buffer_order;
    uint32_t acquired_record_index;
    uint32_t batch_target_records;
    uint32_t records_in_batch;
    uint8_t filling_buffer_index;
    uint8_t acquired_buffer_index;
    volatile task_storage_media_state_t media_state;
    volatile bool recording_active;
    bool record_acquired;
    bool started;
} task_storage_state_t;

static task_storage_state_t s_storage;
static portMUX_TYPE s_record_buffer_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_next_command_identifier;
static uint8_t s_static_header[RECORDING_HEADER_STATIC_SIZE_BYTES];
static const uint8_t s_empty_header_sector[ADC_RECORD_SIZE_BYTES];

_Static_assert(TASK_STORAGE_RECORDING_NAME_SIZE_BYTES == 32U,
               "recording names must match the wire protocol");
_Static_assert(TASK_STORAGE_RECORDING_MAX_COUNT == 255U,
               "recording limit must match the wire protocol");
_Static_assert(TASK_STORAGE_READ_CHUNK_SIZE_BYTES == 38U,
               "temporary read chunks must fit one command payload");
_Static_assert(TASK_STORAGE_RECORDING_NAME_SIZE_BYTES ==
                   RECORDING_HEADER_RECORDING_NAME_SIZE_BYTES,
               "storage and recording-header names must match");
_Static_assert(TASK_STORAGE_ADC_CHANNEL_COUNT ==
                   RECORDING_HEADER_ADC_CHANNEL_COUNT,
               "storage and recording-header channels must match");
_Static_assert(TASK_STORAGE_CARD_SLOT_COUNT ==
                   RECORDING_HEADER_CARD_SLOT_COUNT,
               "storage and recording-header card slots must match");
_Static_assert((TASK_STORAGE_PSRAM_BUFFER_SIZE_BYTES %
                ADC_RECORD_SIZE_BYTES) == 0U,
               "PSRAM buffers must contain complete ADC records");
_Static_assert(TASK_STORAGE_BATCH_BUFFER_COUNT <
                   TASK_STORAGE_PSRAM_BUFFER_COUNT,
               "one PSRAM buffer must remain available during SD writes");
_Static_assert(TASK_STORAGE_FOUR_CHANNEL_1KSPS_BATCH_RECORDS <=
                   TASK_STORAGE_BATCH_CAPACITY_RECORDS,
               "the five-minute 1 kSPS batch must fit two buffers");
_Static_assert(TASK_STORAGE_FOUR_CHANNEL_FAST_BATCH_RECORDS <=
                   TASK_STORAGE_BATCH_CAPACITY_RECORDS,
               "the faster-rate batch must fit two buffers");

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
            .resource = FW_ERROR_RESOURCE_STORAGE,
            .operation = operation,
            .instance = 0U,
            .detail = detail,
        };
    }
    return status;
}

static uint32_t storage_record_crc32(void *context,
                                     const uint8_t *data,
                                     size_t length_bytes)
{
    (void)context;
    return platform_crc32_le(UINT32_C(0xFFFFFFFF), data, length_bytes) ^
           UINT32_C(0xFFFFFFFF);
}

static fw_status_t initialize_recording_header(
    const storage_command_t *command,
    fw_error_context_t *error)
{
    if (command == NULL || s_storage.active_file == NULL) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }

    recording_header_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    memcpy(metadata.recording_name, command->name,
           sizeof(metadata.recording_name));
    fw_status_t status = platform_device_read_base_mac(
        metadata.device_id, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    metadata.hardware_version = BOARD_HARDWARE_VERSION;
    metadata.hardware_revision = BOARD_HARDWARE_REVISION;
    metadata.firmware_version = APP_FIRMWARE_VERSION;
    metadata.protocol_version = PROTOCOL_VERSION_CURRENT;
    metadata.start_monotonic_timestamp_100ns =
        command->recording_config.start_monotonic_timestamp_100ns;
    metadata.start_unix_timestamp_us = 0U;
    metadata.adc_sample_rate_sps =
        command->recording_config.adc_sample_rate_sps;
    metadata.adc_channel_mask =
        command->recording_config.adc_channel_mask;
    memcpy(metadata.adc_gains, command->recording_config.adc_gains,
           sizeof(metadata.adc_gains));
    memcpy(metadata.card_types, command->recording_config.card_types,
           sizeof(metadata.card_types));
    metadata.gnss_state = command->recording_config.gnss_state;
    metadata.imu_state = command->recording_config.imu_state;
    metadata.imu_averaging_time_ms =
        command->recording_config.imu_averaging_time_ms;
    metadata.power_rail_flags =
        command->recording_config.power_rail_flags;

    if (recording_header_encode_static(
            &metadata, storage_record_crc32, NULL, s_static_header) !=
        RECORDING_HEADER_CODEC_OK) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }
    status = platform_storage_file_write(
        s_storage.active_file, s_static_header, sizeof(s_static_header), error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    const uint32_t remaining_sectors =
        (RECORDING_HEADER_TOTAL_SIZE_BYTES -
         RECORDING_HEADER_STATIC_SIZE_BYTES) /
        sizeof(s_empty_header_sector);
    for (uint32_t sector = 0U; sector < remaining_sectors; sector++) {
        status = platform_storage_file_write(
            s_storage.active_file, s_empty_header_sector,
            sizeof(s_empty_header_sector), error);
        if (status != FW_STATUS_OK) {
            return status;
        }
    }
    status = platform_storage_file_sync(s_storage.active_file, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    s_storage.active_size_bytes = RECORDING_HEADER_TOTAL_SIZE_BYTES;
    return FW_STATUS_OK;
}

static fw_status_t write_buffer_record_crcs(uint8_t *data,
                                            size_t size_bytes,
                                            fw_error_context_t *error)
{
    if (data == NULL || size_bytes == 0U ||
        (size_bytes % ADC_RECORD_SIZE_BYTES) != 0U) {
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_WRITE, (uint32_t)size_bytes);
    }

    const size_t record_count = size_bytes / ADC_RECORD_SIZE_BYTES;
    for (size_t index = 0U; index < record_count; index++) {
        uint8_t *record = data + (index * ADC_RECORD_SIZE_BYTES);
        if (adc_record_write_crc(record, storage_record_crc32, NULL) !=
            ADC_RECORD_CODEC_OK) {
            return set_error(error, FW_STATUS_INTERNAL,
                             FW_ERROR_OPERATION_WRITE, (uint32_t)index);
        }
    }
    return FW_STATUS_OK;
}

static bool name_is_valid(const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES])
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    bool terminated = false;
    for (size_t index = 0U;
         index < TASK_STORAGE_RECORDING_NAME_SIZE_BYTES;
         index++) {
        const uint8_t value = (uint8_t)name[index];
        if (terminated) {
            if (value != 0U) {
                return false;
            }
            continue;
        }
        if (value == 0U) {
            terminated = true;
        } else if (!((value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
                     (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
                     value == (uint8_t)'_' || value == (uint8_t)'-')) {
            return false;
        }
    }
    return terminated;
}

static bool make_recording_path(
    const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    char path[TASK_STORAGE_RECORDING_PATH_SIZE_BYTES])
{
    if (!name_is_valid(name)) {
        return false;
    }
    const int length = snprintf(path, TASK_STORAGE_RECORDING_PATH_SIZE_BYTES,
                                "%s/%s", TASK_STORAGE_RECORDING_DIRECTORY,
                                name);
    return length > 0 && length < TASK_STORAGE_RECORDING_PATH_SIZE_BYTES;
}

static uint32_t batch_interval_seconds(uint32_t sample_rate_sps)
{
    switch (sample_rate_sps) {
    case UINT32_C(500):
    case UINT32_C(1000):
        return UINT32_C(300);
    case UINT32_C(2000):
        return UINT32_C(120);
    case UINT32_C(4000):
        return UINT32_C(60);
    case UINT32_C(8000):
        return UINT32_C(30);
    case UINT32_C(16000):
        return UINT32_C(15);
    default:
        return 0U;
    }
}

static uint8_t conversions_per_record(uint8_t channel_mask)
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

static bool calculate_batch_target_records(uint32_t sample_rate_sps,
                                           uint8_t channel_mask,
                                           uint32_t *target_records)
{
    if (target_records == NULL) {
        return false;
    }
    const uint32_t interval_seconds =
        batch_interval_seconds(sample_rate_sps);
    const uint8_t record_conversions =
        conversions_per_record(channel_mask);
    if (interval_seconds == 0U || record_conversions == 0U) {
        return false;
    }

    const uint64_t conversions =
        (uint64_t)sample_rate_sps * interval_seconds;
    uint64_t records = conversions / record_conversions;
    if ((conversions % record_conversions) != 0U || records == 0U) {
        return false;
    }

    /*
     * Four-channel recording reaches the requested wall-clock cadence.
     * Eight-channel records hold half as many conversions, so cap the batch
     * to the same bounded PSRAM footprint and write twice as often.
     */
    const uint32_t capacity_limit =
        (sample_rate_sps <= UINT32_C(1000)) ?
            TASK_STORAGE_FOUR_CHANNEL_1KSPS_BATCH_RECORDS :
            TASK_STORAGE_FOUR_CHANNEL_FAST_BATCH_RECORDS;
    if (records > capacity_limit) {
        records = capacity_limit;
    }
    if (records > TASK_STORAGE_BATCH_CAPACITY_RECORDS) {
        return false;
    }
    *target_records = (uint32_t)records;
    return true;
}

static void update_media_state(task_storage_media_state_t state)
{
    if (s_storage.media_state == state) {
        return;
    }
    s_storage.media_state = state;
    if (s_storage.config.media_state_changed != NULL) {
        s_storage.config.media_state_changed(state);
    }
}

static fw_status_t ensure_mounted(fw_error_context_t *error)
{
    if (s_storage.storage != NULL) {
        return FW_STATUS_OK;
    }
    fw_status_t status = board_storage_mount(&s_storage.storage, error);
    if (status != FW_STATUS_OK) {
        s_storage.storage = NULL;
        update_media_state((status == FW_STATUS_MEDIA_ABSENT) ?
                           TASK_STORAGE_MEDIA_ABSENT :
                           TASK_STORAGE_MEDIA_FAULTED);
        return status;
    }
    status = platform_storage_make_directory(
        s_storage.storage, TASK_STORAGE_RECORDING_DIRECTORY, error);
    if (status != FW_STATUS_OK) {
        update_media_state(TASK_STORAGE_MEDIA_FAULTED);
        return status;
    }
    update_media_state(TASK_STORAGE_MEDIA_READY);
    return FW_STATUS_OK;
}

static bool catalog_entry_is_valid(const platform_storage_entry_t *entry)
{
    if (entry == NULL || !entry->is_regular_file ||
        entry->size_bytes > UINT32_MAX ||
        (entry->size_bytes % ADC_RECORD_SIZE_BYTES) != 0U) {
        return false;
    }
    char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES] = {0};
    const size_t length = strlen(entry->name);
    if (length == 0U || length >= sizeof(name)) {
        return false;
    }
    memcpy(name, entry->name, length);
    return name_is_valid(name);
}

static void catalog_insert(const platform_storage_entry_t *entry)
{
    uint16_t position = s_storage.catalog_count;
    while (position > 0U &&
           strcmp(entry->name,
                  s_storage.catalog[position - 1U].name) < 0) {
        s_storage.catalog[position] = s_storage.catalog[position - 1U];
        position--;
    }
    memset(&s_storage.catalog[position], 0,
           sizeof(s_storage.catalog[position]));
    memcpy(s_storage.catalog[position].name, entry->name,
           strlen(entry->name) + 1U);
    s_storage.catalog[position].size_bytes = (uint32_t)entry->size_bytes;
    s_storage.catalog_count++;
}

static fw_status_t catalog_refresh(fw_error_context_t *error)
{
    fw_status_t status = ensure_mounted(error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    memset(s_storage.catalog, 0, sizeof(s_storage.catalog));
    s_storage.catalog_count = 0U;

    platform_storage_directory_t *directory = NULL;
    status = platform_storage_directory_open(
        s_storage.storage, TASK_STORAGE_RECORDING_DIRECTORY,
        &directory, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    for (;;) {
        platform_storage_entry_t entry;
        status = platform_storage_directory_read(directory, &entry, error);
        if (status == FW_STATUS_NOT_FOUND) {
            status = FW_STATUS_OK;
            clear_error(error);
            break;
        }
        if (status != FW_STATUS_OK) {
            break;
        }
        if (!catalog_entry_is_valid(&entry)) {
            continue;
        }
        if (s_storage.catalog_count >= TASK_STORAGE_RECORDING_MAX_COUNT) {
            status = set_error(error, FW_STATUS_OVERFLOW,
                               FW_ERROR_OPERATION_READ,
                               s_storage.catalog_count);
            break;
        }
        catalog_insert(&entry);
    }

    fw_error_context_t close_error;
    clear_error(&close_error);
    const fw_status_t close_status =
        platform_storage_directory_close(directory, &close_error);
    if (status == FW_STATUS_OK && close_status != FW_STATUS_OK) {
        status = close_status;
        if (error != NULL) {
            *error = close_error;
        }
    }
    if (status != FW_STATUS_OK) {
        update_media_state(TASK_STORAGE_MEDIA_FAULTED);
    }
    return status;
}

static void reset_record_buffers_locked(void)
{
    for (uint8_t index = 0U;
         index < TASK_STORAGE_PSRAM_BUFFER_COUNT;
         index++) {
        s_storage.record_buffers[index].committed_records = 0U;
        s_storage.record_buffers[index].order = 0U;
        s_storage.record_buffers[index].state = RECORD_BUFFER_FREE;
    }
    s_storage.filling_buffer_index = TASK_STORAGE_INVALID_BUFFER_INDEX;
    s_storage.acquired_buffer_index = TASK_STORAGE_INVALID_BUFFER_INDEX;
    s_storage.acquired_record_index = 0U;
    s_storage.next_buffer_order = 0U;
    s_storage.records_in_batch = 0U;
    s_storage.record_acquired = false;
}

static bool activate_free_buffer_locked(void)
{
    if (s_storage.filling_buffer_index !=
        TASK_STORAGE_INVALID_BUFFER_INDEX) {
        return true;
    }
    for (uint8_t index = 0U;
         index < TASK_STORAGE_PSRAM_BUFFER_COUNT;
         index++) {
        record_buffer_t *buffer = &s_storage.record_buffers[index];
        if (buffer->state != RECORD_BUFFER_FREE) {
            continue;
        }
        buffer->committed_records = 0U;
        buffer->order = s_storage.next_buffer_order++;
        buffer->state = RECORD_BUFFER_FILLING;
        s_storage.filling_buffer_index = index;
        return true;
    }
    return false;
}

static void start_record_buffers(uint32_t batch_target_records)
{
    portENTER_CRITICAL(&s_record_buffer_lock);
    reset_record_buffers_locked();
    s_storage.batch_target_records = batch_target_records;
    (void)activate_free_buffer_locked();
    s_storage.recording_active = true;
    portEXIT_CRITICAL(&s_record_buffer_lock);
}

static void discard_record_buffers(void)
{
    portENTER_CRITICAL(&s_record_buffer_lock);
    s_storage.recording_active = false;
    reset_record_buffers_locked();
    s_storage.batch_target_records = 0U;
    portEXIT_CRITICAL(&s_record_buffer_lock);
}

static void make_sealed_buffers_ready_locked(void)
{
    for (uint8_t index = 0U;
         index < TASK_STORAGE_PSRAM_BUFFER_COUNT;
         index++) {
        if (s_storage.record_buffers[index].state ==
            RECORD_BUFFER_SEALED) {
            s_storage.record_buffers[index].state = RECORD_BUFFER_READY;
        }
    }
}

static bool seal_record_buffers(void)
{
    bool sealed = false;
    portENTER_CRITICAL(&s_record_buffer_lock);
    if (!s_storage.record_acquired) {
        if (s_storage.filling_buffer_index !=
            TASK_STORAGE_INVALID_BUFFER_INDEX) {
            record_buffer_t *buffer = &s_storage.record_buffers[
                s_storage.filling_buffer_index];
            buffer->state = (buffer->committed_records == 0U) ?
                RECORD_BUFFER_FREE : RECORD_BUFFER_READY;
            s_storage.filling_buffer_index =
                TASK_STORAGE_INVALID_BUFFER_INDEX;
        }
        make_sealed_buffers_ready_locked();
        s_storage.records_in_batch = 0U;
        s_storage.recording_active = false;
        sealed = true;
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);
    return sealed;
}

static bool claim_oldest_ready_buffer(uint8_t *index,
                                      uint8_t **data,
                                      size_t *size_bytes)
{
    bool found = false;
    uint8_t oldest = TASK_STORAGE_INVALID_BUFFER_INDEX;
    uint32_t oldest_order = 0U;
    portENTER_CRITICAL(&s_record_buffer_lock);
    for (uint8_t candidate = 0U;
         candidate < TASK_STORAGE_PSRAM_BUFFER_COUNT;
         candidate++) {
        const record_buffer_t *buffer =
            &s_storage.record_buffers[candidate];
        if (buffer->state == RECORD_BUFFER_READY &&
            (oldest == TASK_STORAGE_INVALID_BUFFER_INDEX ||
             buffer->order < oldest_order)) {
            oldest = candidate;
            oldest_order = buffer->order;
        }
    }
    if (oldest != TASK_STORAGE_INVALID_BUFFER_INDEX) {
        record_buffer_t *buffer = &s_storage.record_buffers[oldest];
        buffer->state = RECORD_BUFFER_WRITING;
        *index = oldest;
        *data = buffer->data;
        *size_bytes =
            (size_t)buffer->committed_records * ADC_RECORD_SIZE_BYTES;
        found = true;
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);
    return found;
}

static void release_written_buffer(uint8_t index)
{
    portENTER_CRITICAL(&s_record_buffer_lock);
    record_buffer_t *buffer = &s_storage.record_buffers[index];
    buffer->committed_records = 0U;
    buffer->state = RECORD_BUFFER_FREE;
    if (s_storage.recording_active) {
        (void)activate_free_buffer_locked();
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);
}

static bool ready_buffer_available(void)
{
    bool ready = false;
    portENTER_CRITICAL(&s_record_buffer_lock);
    for (uint8_t index = 0U;
         index < TASK_STORAGE_PSRAM_BUFFER_COUNT;
         index++) {
        if (s_storage.record_buffers[index].state == RECORD_BUFFER_READY) {
            ready = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);
    return ready;
}

static uint32_t buffered_size_bytes(void)
{
    uint32_t size_bytes = 0U;
    portENTER_CRITICAL(&s_record_buffer_lock);
    for (uint8_t index = 0U;
         index < TASK_STORAGE_PSRAM_BUFFER_COUNT;
         index++) {
        size_bytes += s_storage.record_buffers[index].committed_records *
                      ADC_RECORD_SIZE_BYTES;
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);
    return size_bytes;
}

static void fail_active_recording(fw_status_t failure_status)
{
    discard_record_buffers();
    if (s_storage.config.recording_failed != NULL) {
        s_storage.config.recording_failed(failure_status);
    }

    if (s_storage.active_file != NULL) {
        (void)platform_storage_file_truncate(
            s_storage.active_file, s_storage.active_size_bytes, NULL);
        (void)platform_storage_file_sync(s_storage.active_file, NULL);
        (void)platform_storage_file_close(s_storage.active_file, NULL);
    }
    s_storage.active_file = NULL;
    memset(s_storage.active_name, 0, sizeof(s_storage.active_name));
    s_storage.active_size_bytes = 0U;
}

static fw_status_t write_one_ready_buffer(fw_error_context_t *error)
{
    uint8_t index = TASK_STORAGE_INVALID_BUFFER_INDEX;
    uint8_t *data = NULL;
    size_t size_bytes = 0U;
    if (!claim_oldest_ready_buffer(&index, &data, &size_bytes)) {
        return FW_STATUS_OK;
    }

    fw_status_t status = FW_STATUS_OK;
    if (s_storage.active_file == NULL || size_bytes == 0U) {
        status = set_error(error, FW_STATUS_INVALID_STATE,
                           FW_ERROR_OPERATION_WRITE, (uint32_t)size_bytes);
    } else if (s_storage.active_size_bytes > UINT32_MAX - size_bytes) {
        status = set_error(error, FW_STATUS_STORAGE_FULL,
                           FW_ERROR_OPERATION_WRITE,
                           s_storage.active_size_bytes);
    } else {
        /*
         * CRC calculation rereads each 508-byte record from PSRAM. Keep that
         * burst inside the scheduled SD-write noise window instead of doing
         * it at the 40-conversion record cadence in task_acquisition.
         */
        status = write_buffer_record_crcs(data, size_bytes, error);
        if (status == FW_STATUS_OK) {
            status = platform_storage_file_write(
                s_storage.active_file, data, size_bytes, error);
        }
    }
    if (status != FW_STATUS_OK) {
        update_media_state(TASK_STORAGE_MEDIA_FAULTED);
        fail_active_recording(status);
        return status;
    }

    s_storage.active_size_bytes += (uint32_t)size_bytes;
    release_written_buffer(index);
    return FW_STATUS_OK;
}

static fw_status_t drain_ready_buffers(bool sync_after_write,
                                       fw_error_context_t *error)
{
    bool wrote_data = false;
    while (ready_buffer_available()) {
        const fw_status_t status = write_one_ready_buffer(error);
        if (status != FW_STATUS_OK) {
            return status;
        }
        wrote_data = true;
    }
    if (sync_after_write && wrote_data) {
        const fw_status_t status =
            platform_storage_file_sync(s_storage.active_file, error);
        if (status != FW_STATUS_OK) {
            update_media_state(TASK_STORAGE_MEDIA_FAULTED);
            fail_active_recording(status);
            return status;
        }
    }
    return FW_STATUS_OK;
}

static fw_status_t close_active_file(bool delete_after_close,
                                     char output_name[
                                         TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
                                     fw_error_context_t *error)
{
    if (!s_storage.recording_active || s_storage.active_file == NULL) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_CLOSE, 0U);
    }
    if (output_name != NULL) {
        memcpy(output_name, s_storage.active_name,
               TASK_STORAGE_RECORDING_NAME_SIZE_BYTES);
    }

    if (!seal_record_buffers()) {
        return set_error(error, FW_STATUS_BUSY,
                         FW_ERROR_OPERATION_CLOSE, 0U);
    }

    fw_status_t status = drain_ready_buffers(false, error);
    if (status == FW_STATUS_OK) {
        status = platform_storage_file_sync(s_storage.active_file, error);
    }
    fw_error_context_t close_error;
    clear_error(&close_error);
    fw_status_t close_status = FW_STATUS_OK;
    if (s_storage.active_file != NULL) {
        close_status = platform_storage_file_close(
            s_storage.active_file, &close_error);
    }
    s_storage.active_file = NULL;

    if (status == FW_STATUS_OK && close_status != FW_STATUS_OK) {
        status = close_status;
        if (error != NULL) {
            *error = close_error;
        }
    }
    if (delete_after_close) {
        char path[TASK_STORAGE_RECORDING_PATH_SIZE_BYTES];
        if (make_recording_path(s_storage.active_name, path)) {
            fw_error_context_t delete_error;
            clear_error(&delete_error);
            const fw_status_t delete_status = platform_storage_file_delete(
                s_storage.storage, path, &delete_error);
            if (status == FW_STATUS_OK && delete_status != FW_STATUS_OK) {
                status = delete_status;
                if (error != NULL) {
                    *error = delete_error;
                }
            }
        }
    }
    memset(s_storage.active_name, 0, sizeof(s_storage.active_name));
    s_storage.active_size_bytes = 0U;
    s_storage.batch_target_records = 0U;
    return status;
}

static void handle_command(const storage_command_t *command,
                           storage_response_t *response)
{
    memset(response, 0, sizeof(*response));
    response->identifier = command->identifier;
    clear_error(&response->error);

    switch (command->operation) {
    case STORAGE_COMMAND_OPEN: {
        if (!name_is_valid(command->name)) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_OPEN, 0U);
            break;
        }
        uint32_t batch_target_records = 0U;
        if (!calculate_batch_target_records(
                command->recording_config.adc_sample_rate_sps,
                command->recording_config.adc_channel_mask,
                &batch_target_records)) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_OPEN,
                command->recording_config.adc_sample_rate_sps);
            break;
        }
        if (s_storage.recording_active) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_STATE,
                FW_ERROR_OPERATION_OPEN, 0U);
            break;
        }
        response->status = catalog_refresh(&response->error);
        if (response->status != FW_STATUS_OK) {
            break;
        }
        if (s_storage.catalog_count >= TASK_STORAGE_RECORDING_MAX_COUNT) {
            response->status = set_error(
                &response->error, FW_STATUS_OVERFLOW,
                FW_ERROR_OPERATION_OPEN, s_storage.catalog_count);
            break;
        }
        char path[TASK_STORAGE_RECORDING_PATH_SIZE_BYTES];
        if (!make_recording_path(command->name, path)) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_OPEN, 0U);
            break;
        }
        response->status = platform_storage_file_open_new(
            s_storage.storage, path, &s_storage.active_file,
            &response->error);
        if (response->status != FW_STATUS_OK) {
            break;
        }
        response->status = initialize_recording_header(
            command, &response->error);
        if (response->status != FW_STATUS_OK) {
            const fw_error_context_t original_error = response->error;
            (void)platform_storage_file_close(
                s_storage.active_file, NULL);
            s_storage.active_file = NULL;
            (void)platform_storage_file_delete(
                s_storage.storage, path, NULL);
            s_storage.active_size_bytes = 0U;
            response->error = original_error;
            break;
        }
        memcpy(s_storage.active_name, command->name,
               sizeof(s_storage.active_name));
        start_record_buffers(batch_target_records);
        memcpy(response->name, command->name, sizeof(response->name));
        break;
    }
    case STORAGE_COMMAND_CLOSE:
        response->status = close_active_file(
            false, response->name, &response->error);
        break;
    case STORAGE_COMMAND_ABORT:
        response->status = close_active_file(
            true, response->name, &response->error);
        break;
    case STORAGE_COMMAND_COUNT:
        response->status = catalog_refresh(&response->error);
        response->count = (response->status == FW_STATUS_OK) ?
                          s_storage.catalog_count : 0U;
        break;
    case STORAGE_COMMAND_INFO:
        response->status = catalog_refresh(&response->error);
        if (response->status != FW_STATUS_OK) {
            break;
        }
        if (command->index >= s_storage.catalog_count) {
            response->status = set_error(
                &response->error, FW_STATUS_NOT_FOUND,
                FW_ERROR_OPERATION_READ, command->index);
            break;
        }
        response->info.index = command->index;
        memcpy(response->info.name,
               s_storage.catalog[command->index].name,
               sizeof(response->info.name));
        response->info.recording_in_progress =
            s_storage.recording_active &&
            strcmp(response->info.name, s_storage.active_name) == 0;
        if (response->info.recording_in_progress) {
            const uint64_t logical_size =
                (uint64_t)s_storage.active_size_bytes +
                buffered_size_bytes();
            response->info.size_bytes = (logical_size > UINT32_MAX) ?
                UINT32_MAX : (uint32_t)logical_size;
        } else {
            response->info.size_bytes =
                s_storage.catalog[command->index].size_bytes;
        }
        response->info.start_unix_timestamp_us = 0U;
        break;
    case STORAGE_COMMAND_DELETE: {
        if (!name_is_valid(command->name)) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_DELETE, 0U);
            break;
        }
        memcpy(response->name, command->name, sizeof(response->name));
        if (s_storage.recording_active &&
            strcmp(command->name, s_storage.active_name) == 0) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_STATE,
                FW_ERROR_OPERATION_DELETE, 0U);
            break;
        }
        response->status = ensure_mounted(&response->error);
        if (response->status != FW_STATUS_OK) {
            break;
        }
        char path[TASK_STORAGE_RECORDING_PATH_SIZE_BYTES];
        if (!make_recording_path(command->name, path)) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_DELETE, 0U);
            break;
        }
        response->status = platform_storage_file_delete(
            s_storage.storage, path, &response->error);
        break;
    }
    case STORAGE_COMMAND_READ: {
        response->chunk.offset_bytes = command->offset_bytes;
        if (!name_is_valid(command->name)) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_READ, 0U);
            break;
        }
        if (s_storage.recording_active) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_STATE,
                FW_ERROR_OPERATION_READ, 0U);
            break;
        }
        response->status = ensure_mounted(&response->error);
        if (response->status != FW_STATUS_OK) {
            break;
        }
        char path[TASK_STORAGE_RECORDING_PATH_SIZE_BYTES];
        if (!make_recording_path(command->name, path)) {
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_READ, 0U);
            break;
        }

        uint64_t file_size = 0U;
        bool is_regular_file = false;
        response->status = platform_storage_file_stat(
            s_storage.storage, path, &file_size, &is_regular_file,
            &response->error);
        if (response->status != FW_STATUS_OK) {
            break;
        }
        if (!is_regular_file || file_size > UINT32_MAX) {
            response->status = set_error(
                &response->error, FW_STATUS_INTEGRITY,
                FW_ERROR_OPERATION_READ,
                (file_size > UINT32_MAX) ? UINT32_MAX : 0U);
            break;
        }
        response->chunk.file_size_bytes = (uint32_t)file_size;
        if (command->offset_bytes > response->chunk.file_size_bytes) {
            response->chunk.file_size_bytes = 0U;
            response->status = set_error(
                &response->error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_READ, command->offset_bytes);
            break;
        }

        platform_storage_file_t *file = NULL;
        response->status = platform_storage_file_open_read(
            s_storage.storage, path, &file, &response->error);
        if (response->status != FW_STATUS_OK) {
            response->chunk.file_size_bytes = 0U;
            break;
        }
        size_t bytes_read = 0U;
        response->status = platform_storage_file_read(
            file, command->offset_bytes, response->chunk.data,
            sizeof(response->chunk.data), &bytes_read, &response->error);
        fw_error_context_t close_error;
        clear_error(&close_error);
        const fw_status_t close_status = platform_storage_file_close(
            file, &close_error);
        if (response->status == FW_STATUS_OK &&
            close_status != FW_STATUS_OK) {
            response->status = close_status;
            response->error = close_error;
        }
        if (response->status != FW_STATUS_OK) {
            memset(&response->chunk, 0, sizeof(response->chunk));
            response->chunk.offset_bytes = command->offset_bytes;
            break;
        }
        response->chunk.data_length_bytes = (uint8_t)bytes_read;
        break;
    }
    default:
        response->status = set_error(
            &response->error, FW_STATUS_INTERNAL,
            FW_ERROR_OPERATION_NONE, (uint32_t)command->operation);
        break;
    }
}

static void storage_task_run(void *context)
{
    (void)context;
    (void)ensure_mounted(NULL);

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        storage_command_t command;
        if (xQueueReceive(s_storage.commands, &command, 0U) == pdTRUE) {
            storage_response_t response;
            handle_command(&command, &response);
            (void)xQueueSend(s_storage.responses, &response, portMAX_DELAY);
            if (uxQueueMessagesWaiting(s_storage.commands) > 0U ||
                ready_buffer_available()) {
                xTaskNotifyGive(s_storage.task_handle);
            }
            continue;
        }

        /* One wakeup owns the complete scheduled SD-write noise window. */
        (void)drain_ready_buffers(true, NULL);
    }
}

fw_status_t task_storage_start(const task_storage_config_t *config,
                               fw_error_context_t *error)
{
    clear_error(error);
    if (config == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    if (s_storage.started) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    memset(&s_storage, 0, sizeof(s_storage));
    s_storage.config = *config;
    s_storage.media_state = TASK_STORAGE_MEDIA_ABSENT;
    s_storage.commands = xQueueCreate(
        TASK_STORAGE_COMMAND_QUEUE_LENGTH, sizeof(storage_command_t));
    s_storage.responses = xQueueCreate(
        TASK_STORAGE_RESPONSE_QUEUE_LENGTH, sizeof(storage_response_t));
    if (s_storage.commands == NULL || s_storage.responses == NULL) {
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    for (uint8_t index = 0U;
         index < TASK_STORAGE_PSRAM_BUFFER_COUNT;
         index++) {
        s_storage.record_buffers[index].data =
            platform_memory_allocate_external(
                TASK_STORAGE_PSRAM_BUFFER_SIZE_BYTES, 64U);
        if (s_storage.record_buffers[index].data == NULL) {
            for (uint8_t allocated = 0U;
                 allocated < index;
                 allocated++) {
                platform_memory_free(
                    s_storage.record_buffers[allocated].data);
                s_storage.record_buffers[allocated].data = NULL;
            }
            vQueueDelete(s_storage.commands);
            vQueueDelete(s_storage.responses);
            s_storage.commands = NULL;
            s_storage.responses = NULL;
            return set_error(error, FW_STATUS_INTERNAL,
                             FW_ERROR_OPERATION_INITIALIZE,
                             TASK_STORAGE_PSRAM_BUFFER_SIZE_BYTES);
        }
    }
    portENTER_CRITICAL(&s_record_buffer_lock);
    reset_record_buffers_locked();
    portEXIT_CRITICAL(&s_record_buffer_lock);
    if (xTaskCreate(storage_task_run, "storage",
                    TASK_STORAGE_STACK_SIZE_BYTES, NULL,
                    TASK_STORAGE_PRIORITY,
                    &s_storage.task_handle) != pdPASS) {
        for (uint8_t index = 0U;
             index < TASK_STORAGE_PSRAM_BUFFER_COUNT;
             index++) {
            platform_memory_free(s_storage.record_buffers[index].data);
            s_storage.record_buffers[index].data = NULL;
        }
        vQueueDelete(s_storage.commands);
        vQueueDelete(s_storage.responses);
        s_storage.commands = NULL;
        s_storage.responses = NULL;
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE,
                         TASK_STORAGE_STACK_SIZE_BYTES);
    }
    s_storage.started = true;
    return FW_STATUS_OK;
}

static fw_status_t execute_command(storage_command_t *command,
                                   storage_response_t *response,
                                   fw_error_context_t *error)
{
    clear_error(error);
    if (!s_storage.started || command == NULL || response == NULL) {
        return set_error(error,
                         s_storage.started ? FW_STATUS_INVALID_ARGUMENT :
                                             FW_STATUS_NOT_INITIALIZED,
                         FW_ERROR_OPERATION_WAIT, 0U);
    }
    memset(response, 0, sizeof(*response));
    command->identifier = ++s_next_command_identifier;
    const TickType_t timeout = pdMS_TO_TICKS(TASK_STORAGE_COMMAND_TIMEOUT_MS);
    if (xQueueSend(s_storage.commands, command, timeout) != pdTRUE) {
        return set_error(error, FW_STATUS_TIMEOUT,
                         FW_ERROR_OPERATION_WAIT, command->identifier);
    }
    xTaskNotifyGive(s_storage.task_handle);
    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout ||
            xQueueReceive(s_storage.responses, response,
                          timeout - elapsed) != pdTRUE) {
            return set_error(error, FW_STATUS_TIMEOUT,
                             FW_ERROR_OPERATION_WAIT,
                             command->identifier);
        }
        if (response->identifier == command->identifier) {
            if (response->status != FW_STATUS_OK && error != NULL) {
                *error = response->error;
            }
            return response->status;
        }
    }
}

fw_status_t task_storage_recording_open(
    const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    const task_storage_recording_config_t *recording_config,
    fw_error_context_t *error)
{
    storage_command_t command = {.operation = STORAGE_COMMAND_OPEN};
    storage_response_t response;
    if (name == NULL || recording_config == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_OPEN, 0U);
    }
    command.recording_config = *recording_config;
    memcpy(command.name, name, sizeof(command.name));
    return execute_command(&command, &response, error);
}

fw_status_t task_storage_recording_close(
    char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    fw_error_context_t *error)
{
    storage_command_t command = {.operation = STORAGE_COMMAND_CLOSE};
    storage_response_t response;
    if (name == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_CLOSE, 0U);
    }
    memset(name, 0, TASK_STORAGE_RECORDING_NAME_SIZE_BYTES);
    const fw_status_t status = execute_command(&command, &response, error);
    memcpy(name, response.name, TASK_STORAGE_RECORDING_NAME_SIZE_BYTES);
    return status;
}

fw_status_t task_storage_recording_abort(fw_error_context_t *error)
{
    storage_command_t command = {.operation = STORAGE_COMMAND_ABORT};
    storage_response_t response;
    return execute_command(&command, &response, error);
}

fw_status_t task_storage_recording_count(uint16_t *count,
                                         fw_error_context_t *error)
{
    storage_command_t command = {.operation = STORAGE_COMMAND_COUNT};
    storage_response_t response;
    if (count == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, 0U);
    }
    *count = 0U;
    const fw_status_t status = execute_command(&command, &response, error);
    if (status == FW_STATUS_OK) {
        *count = response.count;
    }
    return status;
}

fw_status_t task_storage_recording_info(
    uint16_t index,
    task_storage_recording_info_t *info,
    fw_error_context_t *error)
{
    storage_command_t command = {
        .operation = STORAGE_COMMAND_INFO,
        .index = index,
    };
    storage_response_t response;
    if (info == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, index);
    }
    memset(info, 0, sizeof(*info));
    const fw_status_t status = execute_command(&command, &response, error);
    if (status == FW_STATUS_OK) {
        *info = response.info;
    }
    return status;
}

fw_status_t task_storage_recording_delete(
    const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    fw_error_context_t *error)
{
    storage_command_t command = {.operation = STORAGE_COMMAND_DELETE};
    storage_response_t response;
    if (name == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_DELETE, 0U);
    }
    memcpy(command.name, name, sizeof(command.name));
    return execute_command(&command, &response, error);
}

fw_status_t task_storage_recording_read_chunk(
    const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    uint32_t offset_bytes,
    task_storage_recording_chunk_t *chunk,
    fw_error_context_t *error)
{
    storage_command_t command = {
        .operation = STORAGE_COMMAND_READ,
        .offset_bytes = offset_bytes,
    };
    storage_response_t response;
    if (name == NULL || chunk == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, offset_bytes);
    }
    memset(chunk, 0, sizeof(*chunk));
    chunk->offset_bytes = offset_bytes;
    memcpy(command.name, name, sizeof(command.name));
    const fw_status_t status = execute_command(&command, &response, error);
    if (status == FW_STATUS_OK) {
        *chunk = response.chunk;
    }
    return status;
}

fw_status_t task_storage_record_acquire(
    uint8_t **record,
    fw_error_context_t *error)
{
    clear_error(error);
    if (record == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, 0U);
    }
    *record = NULL;
    if (!s_storage.started) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_READ, 0U);
    }

    fw_status_t status = FW_STATUS_OK;
    portENTER_CRITICAL(&s_record_buffer_lock);
    if (!s_storage.recording_active) {
        status = FW_STATUS_INVALID_STATE;
    } else if (s_storage.record_acquired) {
        status = FW_STATUS_INVALID_STATE;
    } else if (!activate_free_buffer_locked()) {
        status = FW_STATUS_OVERFLOW;
    } else {
        const uint8_t index = s_storage.filling_buffer_index;
        record_buffer_t *buffer = &s_storage.record_buffers[index];
        if (buffer->state != RECORD_BUFFER_FILLING ||
            buffer->committed_records >=
                TASK_STORAGE_RECORDS_PER_PSRAM_BUFFER) {
            status = FW_STATUS_INTERNAL;
        } else {
            s_storage.record_acquired = true;
            s_storage.acquired_buffer_index = index;
            s_storage.acquired_record_index = buffer->committed_records;
            *record = buffer->data +
                ((size_t)buffer->committed_records *
                 ADC_RECORD_SIZE_BYTES);
        }
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);
    return (status == FW_STATUS_OK) ? FW_STATUS_OK :
        set_error(error, status, FW_ERROR_OPERATION_READ, 0U);
}

fw_status_t task_storage_record_submit(uint8_t *record,
                                       fw_error_context_t *error)
{
    clear_error(error);
    if (record == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }

    fw_status_t status = FW_STATUS_OK;
    bool batch_ready = false;
    portENTER_CRITICAL(&s_record_buffer_lock);
    if (!s_storage.recording_active || !s_storage.record_acquired) {
        status = FW_STATUS_INVALID_STATE;
    } else if (s_storage.acquired_buffer_index >=
               TASK_STORAGE_PSRAM_BUFFER_COUNT) {
        status = FW_STATUS_INTERNAL;
    } else if (s_storage.batch_target_records == 0U ||
               s_storage.records_in_batch >=
                   s_storage.batch_target_records) {
        status = FW_STATUS_INTERNAL;
    } else {
        const uint8_t index = s_storage.acquired_buffer_index;
        record_buffer_t *buffer = &s_storage.record_buffers[index];
        uint8_t *expected = buffer->data +
            ((size_t)s_storage.acquired_record_index *
             ADC_RECORD_SIZE_BYTES);
        if (record != expected || buffer->state != RECORD_BUFFER_FILLING ||
            buffer->committed_records !=
                s_storage.acquired_record_index) {
            status = FW_STATUS_INVALID_ARGUMENT;
        } else {
            buffer->committed_records++;
            s_storage.records_in_batch++;
            s_storage.record_acquired = false;
            s_storage.acquired_buffer_index =
                TASK_STORAGE_INVALID_BUFFER_INDEX;
            s_storage.acquired_record_index = 0U;
            if (s_storage.records_in_batch ==
                s_storage.batch_target_records) {
                buffer->state = RECORD_BUFFER_READY;
                s_storage.filling_buffer_index =
                    TASK_STORAGE_INVALID_BUFFER_INDEX;
                make_sealed_buffers_ready_locked();
                s_storage.records_in_batch = 0U;
                (void)activate_free_buffer_locked();
                batch_ready = true;
            } else if (buffer->committed_records ==
                       TASK_STORAGE_RECORDS_PER_PSRAM_BUFFER) {
                buffer->state = RECORD_BUFFER_SEALED;
                s_storage.filling_buffer_index =
                    TASK_STORAGE_INVALID_BUFFER_INDEX;
                (void)activate_free_buffer_locked();
            }
        }
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);

    if (status != FW_STATUS_OK) {
        task_storage_record_release(record);
        return set_error(error, status, FW_ERROR_OPERATION_WRITE, 0U);
    }
    if (batch_ready) {
        xTaskNotifyGive(s_storage.task_handle);
    }
    return FW_STATUS_OK;
}

void task_storage_record_release(uint8_t *record)
{
    if (record == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_record_buffer_lock);
    if (s_storage.record_acquired &&
        s_storage.acquired_buffer_index <
            TASK_STORAGE_PSRAM_BUFFER_COUNT) {
        const record_buffer_t *buffer = &s_storage.record_buffers[
            s_storage.acquired_buffer_index];
        const uint8_t *expected = buffer->data +
            ((size_t)s_storage.acquired_record_index *
             ADC_RECORD_SIZE_BYTES);
        if (record == expected) {
            s_storage.record_acquired = false;
            s_storage.acquired_buffer_index =
                TASK_STORAGE_INVALID_BUFFER_INDEX;
            s_storage.acquired_record_index = 0U;
        }
    }
    portEXIT_CRITICAL(&s_record_buffer_lock);
}

task_storage_media_state_t task_storage_media_state(void)
{
    return s_storage.media_state;
}

bool task_storage_recording_active(void)
{
    return s_storage.recording_active;
}
