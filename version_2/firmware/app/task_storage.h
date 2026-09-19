#ifndef GEOPHYS_TASK_STORAGE_H
#define GEOPHYS_TASK_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "adc_record.h"
#include "fw_error.h"

#define TASK_STORAGE_RECORDING_NAME_SIZE_BYTES UINT8_C(32)
#define TASK_STORAGE_RECORDING_MAX_COUNT UINT16_C(255)
#define TASK_STORAGE_READ_CHUNK_SIZE_BYTES UINT8_C(38)

typedef enum {
    TASK_STORAGE_MEDIA_ABSENT = 0,
    TASK_STORAGE_MEDIA_READY,
    TASK_STORAGE_MEDIA_FAULTED,
} task_storage_media_state_t;

typedef void (*task_storage_media_state_callback_t)(
    task_storage_media_state_t state);
typedef void (*task_storage_recording_failed_callback_t)(fw_status_t status);

typedef struct {
    task_storage_media_state_callback_t media_state_changed;
    task_storage_recording_failed_callback_t recording_failed;
} task_storage_config_t;

typedef struct {
    uint16_t index;
    bool recording_in_progress;
    char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES];
    uint64_t start_unix_timestamp_us;
    uint32_t size_bytes;
} task_storage_recording_info_t;

typedef struct {
    uint32_t file_size_bytes;
    uint32_t offset_bytes;
    uint8_t data_length_bytes;
    uint8_t data[TASK_STORAGE_READ_CHUNK_SIZE_BYTES];
} task_storage_recording_chunk_t;

/** Start the sole filesystem-owner task. Missing media is not a boot failure. */
fw_status_t task_storage_start(const task_storage_config_t *config,
                               fw_error_context_t *error);

fw_status_t task_storage_recording_open(
    const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    fw_error_context_t *error);

fw_status_t task_storage_recording_close(
    char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    fw_error_context_t *error);

/** Close and delete a newly opened file after acquisition startup failed. */
fw_status_t task_storage_recording_abort(fw_error_context_t *error);

fw_status_t task_storage_recording_count(uint16_t *count,
                                         fw_error_context_t *error);

fw_status_t task_storage_recording_info(
    uint16_t index,
    task_storage_recording_info_t *info,
    fw_error_context_t *error);

fw_status_t task_storage_recording_delete(
    const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    fw_error_context_t *error);

/** Temporary test support: read one chunk from a closed recording. */
fw_status_t task_storage_recording_read_chunk(
    const char name[TASK_STORAGE_RECORDING_NAME_SIZE_BYTES],
    uint32_t offset_bytes,
    task_storage_recording_chunk_t *chunk,
    fw_error_context_t *error);

/** Nonblocking fixed-pool operations used only by task_acquisition. */
fw_status_t task_storage_record_acquire(
    uint8_t **record,
    fw_error_context_t *error);
fw_status_t task_storage_record_submit(uint8_t *record,
                                       fw_error_context_t *error);
void task_storage_record_release(uint8_t *record);

task_storage_media_state_t task_storage_media_state(void);
bool task_storage_recording_active(void);

#endif /* GEOPHYS_TASK_STORAGE_H */
