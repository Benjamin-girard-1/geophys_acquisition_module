#ifndef GEOPHYS_TASK_FIELD_RECORDING_H
#define GEOPHYS_TASK_FIELD_RECORDING_H

#include "fw_error.h"

/**
 * Start unattended field recording control.
 *
 * Recording begins after a fixed startup delay. Holding the DevKitC BOOT
 * button stops and closes the recording through recording_controller.
 */
fw_status_t task_field_recording_start(fw_error_context_t *error);

#endif /* GEOPHYS_TASK_FIELD_RECORDING_H */
