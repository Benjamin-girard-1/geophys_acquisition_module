#ifndef GEOPHYS_RECORDING_CONTROLLER_H
#define GEOPHYS_RECORDING_CONTROLLER_H

#include <stdint.h>

#include "fw_error.h"
#include "protocol_messages.h"

/** Initialize the acquisition and storage owners before communication starts. */
fw_status_t recording_controller_initialize(fw_error_context_t *error);

fw_status_t recording_controller_start(
    const protocol_recording_name_t *name,
    protocol_recording_start_result_t *result,
    fw_error_context_t *error);

fw_status_t recording_controller_stop(
    protocol_recording_stop_result_t *result,
    fw_error_context_t *error);

fw_status_t recording_controller_streaming_start(
    const protocol_streaming_start_request_t *request,
    protocol_streaming_start_result_t *result,
    fw_error_context_t *error);

fw_status_t recording_controller_streaming_stop(
    protocol_streaming_stop_result_t *result,
    fw_error_context_t *error);

fw_status_t recording_controller_get_number(
    protocol_recording_number_t *number,
    fw_error_context_t *error);

fw_status_t recording_controller_get_info(
    uint16_t index,
    protocol_recording_info_t *info,
    fw_error_context_t *error);

fw_status_t recording_controller_delete(
    const protocol_recording_name_t *name,
    protocol_recording_delete_result_t *result,
    fw_error_context_t *error);

/** Temporary UART-only extraction command used while removable media is absent. */
fw_status_t recording_controller_read_chunk(
    const protocol_temp_recording_read_request_t *request,
    protocol_temp_recording_read_reply_t *reply,
    fw_error_context_t *error);

#endif /* GEOPHYS_RECORDING_CONTROLLER_H */
