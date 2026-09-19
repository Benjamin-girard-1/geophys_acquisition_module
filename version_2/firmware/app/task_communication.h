#ifndef GEOPHYS_TASK_COMMUNICATION_H
#define GEOPHYS_TASK_COMMUNICATION_H

#include <stdint.h>

#include "device_configuration.h"
#include "fw_error.h"
#include "protocol_messages.h"
#include "transport.h"

typedef fw_status_t (*task_communication_get_device_config_callback_t)(
    device_configuration_snapshot_t *snapshot,
    fw_error_context_t *error);

typedef fw_status_t (*task_communication_apply_device_config_callback_t)(
    const device_configuration_update_t *update,
    fw_error_context_t *error);

typedef fw_status_t (*task_communication_streaming_start_callback_t)(
    const protocol_streaming_start_request_t *request,
    protocol_streaming_start_result_t *result,
    fw_error_context_t *error);
typedef fw_status_t (*task_communication_streaming_stop_callback_t)(
    protocol_streaming_stop_result_t *result,
    fw_error_context_t *error);
typedef fw_status_t (*task_communication_stream_record_take_callback_t)(
    uint8_t **record,
    fw_error_context_t *error);
typedef void (*task_communication_stream_record_release_callback_t)(
    uint8_t *record);

typedef fw_status_t (*task_communication_recording_start_callback_t)(
    const protocol_recording_name_t *name,
    protocol_recording_start_result_t *result,
    fw_error_context_t *error);
typedef fw_status_t (*task_communication_recording_stop_callback_t)(
    protocol_recording_stop_result_t *result,
    fw_error_context_t *error);
typedef fw_status_t (*task_communication_recording_number_callback_t)(
    protocol_recording_number_t *number,
    fw_error_context_t *error);
typedef fw_status_t (*task_communication_recording_info_callback_t)(
    uint16_t index,
    protocol_recording_info_t *info,
    fw_error_context_t *error);
typedef fw_status_t (*task_communication_recording_delete_callback_t)(
    const protocol_recording_name_t *name,
    protocol_recording_delete_result_t *result,
    fw_error_context_t *error);
typedef fw_status_t (*task_communication_recording_read_callback_t)(
    const protocol_temp_recording_read_request_t *request,
    protocol_temp_recording_read_reply_t *reply,
    fw_error_context_t *error);

typedef struct {
    transport_interface_t transport;
    protocol_crc32_callback_t crc32;
    void *crc_context;
    protocol_device_info_t device_info;
    task_communication_get_device_config_callback_t get_device_config;
    task_communication_apply_device_config_callback_t apply_device_config;
    task_communication_streaming_start_callback_t streaming_start;
    task_communication_streaming_stop_callback_t streaming_stop;
    task_communication_stream_record_take_callback_t stream_record_take;
    task_communication_stream_record_release_callback_t stream_record_release;
    task_communication_recording_start_callback_t recording_start;
    task_communication_recording_stop_callback_t recording_stop;
    task_communication_recording_number_callback_t recording_get_number;
    task_communication_recording_info_callback_t recording_get_info;
    task_communication_recording_delete_callback_t recording_delete;
    task_communication_recording_read_callback_t recording_read;
    uint32_t read_timeout_us;
    uint32_t write_timeout_us;
} task_communication_config_t;

/** Create the sole command-transport owner and copy its startup configuration. */
fw_status_t task_communication_start(
    const task_communication_config_t *config,
    fw_error_context_t *error);

#endif /* GEOPHYS_TASK_COMMUNICATION_H */
