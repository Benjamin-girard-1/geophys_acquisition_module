#include "task_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "platform_storage.h"

#define TASK_STORAGE_STACK_SIZE_BYTES UINT32_C(8192)
#define TASK_STORAGE_PRIORITY (tskIDLE_PRIORITY + 1U)
#define TASK_STORAGE_RECORD_BUFFER_COUNT UINT8_C(64)
#define TASK_STORAGE_COMMAND_QUEUE_LENGTH UINT8_C(4)
#define TASK_STORAGE_RESPONSE_QUEUE_LENGTH UINT8_C(4)
#define TASK_STORAGE_COMMAND_TIMEOUT_MS UINT32_C(10000)
#define TASK_STORAGE_SYNC_INTERVAL_RECORDS UINT32_C(64)
#define TASK_STORAGE_RECORD_WRITE_BATCH_SIZE UINT8_C(8)
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

typedef struct {
    task_storage_config_t config;
    platform_storage_t *storage;
    platform_storage_file_t *active_file;
    QueueHandle_t free_records;
    QueueHandle_t ready_records;
    QueueHandle_t commands;
    QueueHandle_t responses;
    TaskHandle_t task_handle;
    catalog_entry_t catalog[TASK_STORAGE_RECORDING_MAX_COUNT];
    uint16_t catalog_count;
    char active_name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES];
    uint32_t active_size_bytes;
    uint32_t records_since_sync;
    volatile task_storage_media_state_t media_state;
    volatile bool recording_active;
    bool started;
} task_storage_state_t;

static task_storage_state_t s_storage;
static uint8_t s_record_buffers[TASK_STORAGE_RECORD_BUFFER_COUNT]
                               [ADC_RECORD_SIZE_BYTES]
                               __attribute__((aligned(4)));
static uint32_t s_next_command_identifier;

_Static_assert(TASK_STORAGE_RECORDING_NAME_SIZE_BYTES == 32U,
               "recording names must match the wire protocol");
_Static_assert(TASK_STORAGE_RECORDING_MAX_COUNT == 255U,
               "recording limit must match the wire protocol");
_Static_assert(TASK_STORAGE_READ_CHUNK_SIZE_BYTES == 38U,
               "temporary read chunks must fit one command payload");

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

static void fail_active_recording(fw_status_t failure_status)
{
    s_storage.recording_active = false;
    if (s_storage.config.recording_failed != NULL) {
        s_storage.config.recording_failed(failure_status);
    }

    uint8_t *queued_record = NULL;
    while (xQueueReceive(s_storage.ready_records,
                         &queued_record, 0U) == pdTRUE) {
        (void)xQueueSend(s_storage.free_records, &queued_record, 0U);
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
    s_storage.records_since_sync = 0U;
}

static fw_status_t drain_ready_records(size_t maximum_records,
                                       fw_error_context_t *error)
{
    uint8_t *record = NULL;
    size_t records_written = 0U;
    while ((maximum_records == 0U ||
            records_written < maximum_records) &&
           xQueueReceive(s_storage.ready_records, &record, 0U) == pdTRUE) {
        fw_status_t status = FW_STATUS_OK;
        if (!s_storage.recording_active || s_storage.active_file == NULL) {
            status = set_error(error, FW_STATUS_INVALID_STATE,
                               FW_ERROR_OPERATION_WRITE, 0U);
        } else if (s_storage.active_size_bytes >
                   UINT32_MAX - ADC_RECORD_SIZE_BYTES) {
            status = set_error(error, FW_STATUS_STORAGE_FULL,
                               FW_ERROR_OPERATION_WRITE,
                               s_storage.active_size_bytes);
        } else {
            status = platform_storage_file_write(
                s_storage.active_file, record, ADC_RECORD_SIZE_BYTES, error);
        }
        (void)xQueueSend(s_storage.free_records, &record, 0U);
        if (status != FW_STATUS_OK) {
            update_media_state(TASK_STORAGE_MEDIA_FAULTED);
            fail_active_recording(status);
            return status;
        }
        s_storage.active_size_bytes += ADC_RECORD_SIZE_BYTES;
        records_written++;
        s_storage.records_since_sync++;
        if (s_storage.records_since_sync >=
            TASK_STORAGE_SYNC_INTERVAL_RECORDS) {
            status = platform_storage_file_sync(s_storage.active_file, error);
            if (status != FW_STATUS_OK) {
                update_media_state(TASK_STORAGE_MEDIA_FAULTED);
                fail_active_recording(status);
                return status;
            }
            s_storage.records_since_sync = 0U;
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

    fw_status_t status = drain_ready_records(0U, error);
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
    s_storage.recording_active = false;

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
    s_storage.records_since_sync = 0U;
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
        memcpy(s_storage.active_name, command->name,
               sizeof(s_storage.active_name));
        s_storage.active_size_bytes = 0U;
        s_storage.records_since_sync = 0U;
        s_storage.recording_active = true;
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
        response->info.size_bytes =
            response->info.recording_in_progress ?
            s_storage.active_size_bytes :
            s_storage.catalog[command->index].size_bytes;
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
            continue;
        }

        (void)drain_ready_records(
            TASK_STORAGE_RECORD_WRITE_BATCH_SIZE, NULL);
        if (uxQueueMessagesWaiting(s_storage.ready_records) > 0U) {
            /* Keep draining, but leave a 1 ms command/control opportunity. */
            xTaskNotifyGive(s_storage.task_handle);
            vTaskDelay(1U);
        }
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
    s_storage.free_records = xQueueCreate(
        TASK_STORAGE_RECORD_BUFFER_COUNT, sizeof(uint8_t *));
    s_storage.ready_records = xQueueCreate(
        TASK_STORAGE_RECORD_BUFFER_COUNT, sizeof(uint8_t *));
    s_storage.commands = xQueueCreate(
        TASK_STORAGE_COMMAND_QUEUE_LENGTH, sizeof(storage_command_t));
    s_storage.responses = xQueueCreate(
        TASK_STORAGE_RESPONSE_QUEUE_LENGTH, sizeof(storage_response_t));
    if (s_storage.free_records == NULL || s_storage.ready_records == NULL ||
        s_storage.commands == NULL || s_storage.responses == NULL) {
        return set_error(error, FW_STATUS_INTERNAL,
                         FW_ERROR_OPERATION_INITIALIZE, 0U);
    }
    for (uint8_t index = 0U;
         index < TASK_STORAGE_RECORD_BUFFER_COUNT;
         index++) {
        uint8_t *record = s_record_buffers[index];
        if (xQueueSend(s_storage.free_records, &record, 0U) != pdTRUE) {
            return set_error(error, FW_STATUS_INTERNAL,
                             FW_ERROR_OPERATION_INITIALIZE, index);
        }
    }
    if (xTaskCreate(storage_task_run, "storage",
                    TASK_STORAGE_STACK_SIZE_BYTES, NULL,
                    TASK_STORAGE_PRIORITY,
                    &s_storage.task_handle) != pdPASS) {
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
    fw_error_context_t *error)
{
    storage_command_t command = {.operation = STORAGE_COMMAND_OPEN};
    storage_response_t response;
    if (name == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_OPEN, 0U);
    }
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
    if (!s_storage.started || !s_storage.recording_active) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_READ, 0U);
    }
    if (xQueueReceive(s_storage.free_records, record, 0U) != pdTRUE) {
        return set_error(error, FW_STATUS_OVERFLOW,
                         FW_ERROR_OPERATION_READ, 0U);
    }
    return FW_STATUS_OK;
}

fw_status_t task_storage_record_submit(uint8_t *record,
                                       fw_error_context_t *error)
{
    clear_error(error);
    if (record == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }
    if (!s_storage.recording_active ||
        xQueueSend(s_storage.ready_records, &record, 0U) != pdTRUE) {
        task_storage_record_release(record);
        return set_error(error, FW_STATUS_OVERFLOW,
                         FW_ERROR_OPERATION_WRITE, 0U);
    }
    xTaskNotifyGive(s_storage.task_handle);
    return FW_STATUS_OK;
}

void task_storage_record_release(uint8_t *record)
{
    if (record != NULL && s_storage.free_records != NULL) {
        (void)xQueueSend(s_storage.free_records, &record, 0U);
    }
}

task_storage_media_state_t task_storage_media_state(void)
{
    return s_storage.media_state;
}

bool task_storage_recording_active(void)
{
    return s_storage.recording_active;
}
