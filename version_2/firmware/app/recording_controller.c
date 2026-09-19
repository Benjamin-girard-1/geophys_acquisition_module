#include "recording_controller.h"

#include <stddef.h>
#include <string.h>

#include "device_configuration.h"
#include "freertos/FreeRTOS.h"
#include "task_acquisition.h"
#include "task_storage.h"

_Static_assert(PROTOCOL_RECORDING_NAME_SIZE_BYTES ==
                   TASK_STORAGE_RECORDING_NAME_SIZE_BYTES,
               "protocol and storage recording names must match");
_Static_assert(PROTOCOL_TEMP_RECORDING_READ_DATA_SIZE_BYTES ==
                   TASK_STORAGE_READ_CHUNK_SIZE_BYTES,
               "protocol and storage read chunks must match");

static portMUX_TYPE s_recording_lock = portMUX_INITIALIZER_UNLOCKED;
static protocol_recording_name_t s_active_name;
static fw_status_t s_storage_failure = FW_STATUS_OK;
static bool s_recording_owned;
static bool s_streaming_owned;

static void remember_recording(
    const protocol_recording_name_t *name,
    fw_status_t storage_failure,
    bool owned)
{
    portENTER_CRITICAL(&s_recording_lock);
    s_active_name = (name != NULL) ? *name : (protocol_recording_name_t) {0};
    s_storage_failure = storage_failure;
    s_recording_owned = owned;
    portEXIT_CRITICAL(&s_recording_lock);
}

static void recording_snapshot(protocol_recording_name_t *name,
                               fw_status_t *storage_failure,
                               bool *owned)
{
    portENTER_CRITICAL(&s_recording_lock);
    if (name != NULL) {
        *name = s_active_name;
    }
    if (storage_failure != NULL) {
        *storage_failure = s_storage_failure;
    }
    if (owned != NULL) {
        *owned = s_recording_owned;
    }
    portEXIT_CRITICAL(&s_recording_lock);
}

static bool streaming_snapshot(void)
{
    bool owned;
    portENTER_CRITICAL(&s_recording_lock);
    owned = s_streaming_owned;
    portEXIT_CRITICAL(&s_recording_lock);
    return owned;
}

static void remember_streaming(bool owned)
{
    portENTER_CRITICAL(&s_recording_lock);
    s_streaming_owned = owned;
    portEXIT_CRITICAL(&s_recording_lock);
}

static void storage_media_state_changed(task_storage_media_state_t state)
{
    switch (state) {
    case TASK_STORAGE_MEDIA_READY:
        device_configuration_set_storage_state(DEVICE_SD_CARD_PRESENT);
        break;
    case TASK_STORAGE_MEDIA_FAULTED:
        device_configuration_set_storage_state(DEVICE_SD_CARD_FAULTED);
        break;
    case TASK_STORAGE_MEDIA_ABSENT:
    default:
        device_configuration_set_storage_state(DEVICE_SD_CARD_ABSENT);
        break;
    }
}

static void storage_recording_failed(fw_status_t status)
{
    portENTER_CRITICAL(&s_recording_lock);
    s_storage_failure = status;
    portEXIT_CRITICAL(&s_recording_lock);
    const bool streaming = streaming_snapshot();
    if (task_acquisition_is_active() && !streaming) {
        (void)task_acquisition_recording_stop(NULL);
    }
    device_configuration_set_acquisition_state(streaming, false);
}

fw_status_t recording_controller_initialize(fw_error_context_t *error)
{
    fw_status_t status = task_acquisition_initialize(error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    const task_storage_config_t storage_config = {
        .media_state_changed = storage_media_state_changed,
        .recording_failed = storage_recording_failed,
    };
    return task_storage_start(&storage_config, error);
}

fw_status_t recording_controller_start(
    const protocol_recording_name_t *name,
    protocol_recording_start_result_t *result,
    fw_error_context_t *error)
{
    if (name == NULL || result == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->name = *name;
    result->recording_in_progress = task_storage_recording_active();
    if (result->recording_in_progress) {
        return FW_STATUS_INVALID_STATE;
    }

    device_configuration_snapshot_t snapshot;
    fw_status_t status = device_configuration_get(&snapshot, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (snapshot.adc_channel_mask == 0U) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    status = task_storage_recording_open(name->bytes, error);
    if (status != FW_STATUS_OK) {
        result->recording_in_progress = task_storage_recording_active();
        return status;
    }
    remember_recording(name, FW_STATUS_OK, true);

    task_acquisition_recording_config_t acquisition_config = {
        .sample_rate_sps = snapshot.adc_sample_rate_sps,
        .channel_mask = snapshot.adc_channel_mask,
    };
    memcpy(acquisition_config.gains, snapshot.adc_gains,
           sizeof(acquisition_config.gains));
    status = task_acquisition_recording_start(&acquisition_config, error);
    if (status != FW_STATUS_OK) {
        fw_error_context_t original_error;
        if (error != NULL) {
            original_error = *error;
        }
        (void)task_storage_recording_abort(NULL);
        remember_recording(NULL, FW_STATUS_OK, false);
        if (error != NULL) {
            *error = original_error;
        }
        return status;
    }

    result->recording_in_progress = true;
    device_configuration_set_acquisition_state(true, true);
    return FW_STATUS_OK;
}

fw_status_t recording_controller_stop(
    protocol_recording_stop_result_t *result,
    fw_error_context_t *error)
{
    if (result == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    fw_status_t storage_failure = FW_STATUS_OK;
    bool recording_owned = false;
    recording_snapshot(&result->name, &storage_failure, &recording_owned);
    if (!task_storage_recording_active() && !recording_owned) {
        return FW_STATUS_INVALID_STATE;
    }

    fw_status_t first_status = FW_STATUS_OK;
    fw_error_context_t first_error;
    memset(&first_error, 0, sizeof(first_error));
    if (streaming_snapshot()) {
        first_status = task_acquisition_streaming_stop(error);
        if (first_status != FW_STATUS_OK && error != NULL) {
            first_error = *error;
        } else if (first_status == FW_STATUS_OK) {
            remember_streaming(false);
        }
    }
    if (task_acquisition_is_active()) {
        fw_error_context_t acquisition_error;
        memset(&acquisition_error, 0, sizeof(acquisition_error));
        const fw_status_t acquisition_status =
            task_acquisition_recording_stop(&acquisition_error);
        if (first_status == FW_STATUS_OK &&
            acquisition_status != FW_STATUS_OK) {
            first_status = acquisition_status;
            first_error = acquisition_error;
        }
    }

    if (task_storage_recording_active()) {
        fw_error_context_t storage_error;
        memset(&storage_error, 0, sizeof(storage_error));
        const fw_status_t storage_status = task_storage_recording_close(
            result->name.bytes, &storage_error);
        if (first_status == FW_STATUS_OK && storage_status != FW_STATUS_OK) {
            first_status = storage_status;
            first_error = storage_error;
        }
    }
    recording_snapshot(NULL, &storage_failure, NULL);
    if (first_status == FW_STATUS_OK && storage_failure != FW_STATUS_OK) {
        first_status = storage_failure;
    }
    remember_recording(NULL, FW_STATUS_OK, false);
    remember_streaming(false);
    device_configuration_set_acquisition_state(false, false);
    if (first_status != FW_STATUS_OK && error != NULL) {
        *error = first_error;
    }
    return first_status;
}

fw_status_t recording_controller_streaming_start(
    const protocol_streaming_start_request_t *request,
    protocol_streaming_start_result_t *result,
    fw_error_context_t *error)
{
    if (request == NULL || result == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->decimation = request->decimation;
    result->channel_mask = request->channel_mask;
    result->recording_in_progress = task_storage_recording_active();
    if (request->channel_mask == PROTOCOL_ADC_CHANNEL_MASK_NONE) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    if (streaming_snapshot()) {
        return FW_STATUS_INVALID_STATE;
    }

    device_configuration_snapshot_t snapshot;
    fw_status_t status = device_configuration_get(&snapshot, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    bool started_acquisition = false;
    if (result->recording_in_progress) {
        if (!task_acquisition_is_active()) {
            return FW_STATUS_INVALID_STATE;
        }
        result->channel_mask = snapshot.adc_channel_mask;
    } else {
        if (task_acquisition_is_active()) {
            return FW_STATUS_INVALID_STATE;
        }
        task_acquisition_recording_config_t acquisition_config = {
            .sample_rate_sps = snapshot.adc_sample_rate_sps,
            .channel_mask = request->channel_mask,
        };
        memcpy(acquisition_config.gains, snapshot.adc_gains,
               sizeof(acquisition_config.gains));
        status = task_acquisition_recording_start(
            &acquisition_config, error);
        if (status != FW_STATUS_OK) {
            return status;
        }
        started_acquisition = true;
    }

    status = task_acquisition_streaming_start(
        request->decimation, result->channel_mask, error);
    if (status != FW_STATUS_OK) {
        if (started_acquisition) {
            fw_error_context_t original_error;
            if (error != NULL) {
                original_error = *error;
            }
            (void)task_acquisition_recording_stop(NULL);
            if (error != NULL) {
                *error = original_error;
            }
        }
        return status;
    }

    remember_streaming(true);
    device_configuration_set_acquisition_state(
        true, result->recording_in_progress != 0U);
    return FW_STATUS_OK;
}

fw_status_t recording_controller_streaming_stop(
    protocol_streaming_stop_result_t *result,
    fw_error_context_t *error)
{
    if (result == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->recording_in_progress = task_storage_recording_active();
    if (!streaming_snapshot()) {
        return FW_STATUS_INVALID_STATE;
    }

    fw_status_t status = task_acquisition_streaming_stop(error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    remember_streaming(false);

    if (!result->recording_in_progress && task_acquisition_is_active()) {
        status = task_acquisition_recording_stop(error);
    }
    device_configuration_set_acquisition_state(
        result->recording_in_progress != 0U,
        result->recording_in_progress != 0U);
    return status;
}

fw_status_t recording_controller_get_number(
    protocol_recording_number_t *number,
    fw_error_context_t *error)
{
    if (number == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(number, 0, sizeof(*number));
    return task_storage_recording_count(&number->recording_count, error);
}

fw_status_t recording_controller_get_info(
    uint16_t index,
    protocol_recording_info_t *info,
    fw_error_context_t *error)
{
    if (info == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));
    info->recording_index = index;
    task_storage_recording_info_t storage_info;
    const fw_status_t status = task_storage_recording_info(
        index, &storage_info, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    info->recording_index = storage_info.index;
    info->recording_in_progress = storage_info.recording_in_progress;
    memcpy(info->name.bytes, storage_info.name, sizeof(info->name.bytes));
    info->start_unix_timestamp_us = storage_info.start_unix_timestamp_us;
    info->size_bytes = storage_info.size_bytes;
    return FW_STATUS_OK;
}

fw_status_t recording_controller_delete(
    const protocol_recording_name_t *name,
    protocol_recording_delete_result_t *result,
    fw_error_context_t *error)
{
    if (name == NULL || result == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->name = *name;
    result->recording_in_progress = task_storage_recording_active();
    return task_storage_recording_delete(name->bytes, error);
}

fw_status_t recording_controller_read_chunk(
    const protocol_temp_recording_read_request_t *request,
    protocol_temp_recording_read_reply_t *reply,
    fw_error_context_t *error)
{
    if (request == NULL || reply == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(reply, 0, sizeof(*reply));
    reply->offset_bytes = request->offset_bytes;

    task_storage_recording_chunk_t chunk;
    const fw_status_t status = task_storage_recording_read_chunk(
        request->name.bytes, request->offset_bytes, &chunk, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    reply->file_size_bytes = chunk.file_size_bytes;
    reply->offset_bytes = chunk.offset_bytes;
    reply->data_length_bytes = chunk.data_length_bytes;
    memcpy(reply->data, chunk.data, chunk.data_length_bytes);
    return FW_STATUS_OK;
}
