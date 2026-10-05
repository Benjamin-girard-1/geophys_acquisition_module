#include "task_field_recording.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol_messages.h"
#include "recording_controller.h"
#include "task_storage.h"

#define TASK_FIELD_RECORDING_STACK_SIZE_BYTES UINT32_C(4096)
#define TASK_FIELD_RECORDING_PRIORITY (tskIDLE_PRIORITY + 1U)
#define TASK_FIELD_RECORDING_START_DELAY_MS UINT32_C(5000)
#define TASK_FIELD_RECORDING_RETRY_DELAY_MS UINT32_C(5000)
#define TASK_FIELD_RECORDING_BUTTON_POLL_MS UINT32_C(50)
#define TASK_FIELD_RECORDING_BUTTON_HOLD_MS UINT32_C(3000)
#define TASK_FIELD_RECORDING_STOP_RETRY_MS UINT32_C(250)
#define TASK_FIELD_RECORDING_STOP_ATTEMPTS UINT8_C(3)
#define TASK_FIELD_RECORDING_NAME_LIMIT TASK_STORAGE_RECORDING_MAX_COUNT

static bool s_started;

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

static bool field_name_index(const char name[PROTOCOL_RECORDING_NAME_SIZE_BYTES],
                             uint16_t *index)
{
    static const char prefix[] = "field";
    if (name == NULL || index == NULL ||
        memcmp(name, prefix, sizeof(prefix) - 1U) != 0) {
        return false;
    }
    for (size_t offset = sizeof(prefix) - 1U; offset < 8U; offset++) {
        if (name[offset] < '0' || name[offset] > '9') {
            return false;
        }
    }
    if (name[8] != '\0') {
        return false;
    }

    const uint16_t parsed =
        (uint16_t)((uint16_t)(name[5] - '0') * UINT16_C(100) +
                   (uint16_t)(name[6] - '0') * UINT16_C(10) +
                   (uint16_t)(name[7] - '0'));
    if (parsed >= TASK_FIELD_RECORDING_NAME_LIMIT) {
        return false;
    }
    *index = parsed;
    return true;
}

static fw_status_t next_recording_name(protocol_recording_name_t *name,
                                       fw_error_context_t *error)
{
    if (name == NULL) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    memset(name, 0, sizeof(*name));

    protocol_recording_number_t number;
    fw_status_t status = recording_controller_get_number(&number, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    bool used[TASK_FIELD_RECORDING_NAME_LIMIT] = {false};
    for (uint16_t catalog_index = 0U;
         catalog_index < number.recording_count;
         catalog_index++) {
        protocol_recording_info_t info;
        status = recording_controller_get_info(catalog_index, &info, error);
        if (status != FW_STATUS_OK) {
            return status;
        }
        uint16_t name_index = 0U;
        if (field_name_index(info.name.bytes, &name_index)) {
            used[name_index] = true;
        }
    }

    for (uint16_t name_index = 0U;
         name_index < TASK_FIELD_RECORDING_NAME_LIMIT;
         name_index++) {
        if (!used[name_index]) {
            const int length = snprintf(
                name->bytes, sizeof(name->bytes), "field%03u",
                (unsigned int)name_index);
            return (length == 8) ? FW_STATUS_OK : FW_STATUS_INTERNAL;
        }
    }
    return FW_STATUS_OVERFLOW;
}

static fw_status_t start_recording(fw_error_context_t *error)
{
    protocol_recording_name_t name;
    fw_status_t status = next_recording_name(&name, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    protocol_recording_start_result_t result;
    return recording_controller_start(&name, &result, error);
}

static void stop_recording(void)
{
    for (uint8_t attempt = 0U;
         attempt < TASK_FIELD_RECORDING_STOP_ATTEMPTS;
         attempt++) {
        protocol_recording_stop_result_t result;
        (void)recording_controller_stop(&result, NULL);
        if (!task_storage_recording_active()) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(TASK_FIELD_RECORDING_STOP_RETRY_MS));
    }
}

static void field_recording_task(void *context)
{
    (void)context;
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(TASK_FIELD_RECORDING_BUTTON_POLL_MS);
    const TickType_t hold_ticks =
        pdMS_TO_TICKS(TASK_FIELD_RECORDING_BUTTON_HOLD_MS);
    const TickType_t start_delay_ticks =
        pdMS_TO_TICKS(TASK_FIELD_RECORDING_START_DELAY_MS);
    const TickType_t retry_delay_ticks =
        pdMS_TO_TICKS(TASK_FIELD_RECORDING_RETRY_DELAY_MS);
    const TickType_t boot_tick = xTaskGetTickCount();
    TickType_t press_tick = 0U;
    TickType_t last_start_attempt_tick = 0U;
    bool button_released = false;
    bool press_active = false;
    bool stop_latched = false;
    bool start_failed_with_ready_media = false;

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        bool pressed = false;
        if (board_boot_button_pressed(&pressed, NULL) == FW_STATUS_OK) {
            if (!pressed) {
                button_released = true;
                press_active = false;
            } else if (button_released && !stop_latched) {
                if (!press_active) {
                    press_tick = now;
                    press_active = true;
                } else if ((now - press_tick) >= hold_ticks) {
                    stop_latched = true;
                    stop_recording();
                }
            }
        }

        const bool startup_delay_elapsed =
            (now - boot_tick) >= start_delay_ticks;
        const bool retry_delay_elapsed =
            last_start_attempt_tick == 0U ||
            (now - last_start_attempt_tick) >= retry_delay_ticks;
        if (!stop_latched && !start_failed_with_ready_media &&
            startup_delay_elapsed && retry_delay_elapsed &&
            !task_storage_recording_active()) {
            last_start_attempt_tick = now;
            const fw_status_t status = start_recording(NULL);
            if (status != FW_STATUS_OK &&
                task_storage_media_state() == TASK_STORAGE_MEDIA_READY) {
                /* Avoid creating many files when ADC startup itself fails. */
                start_failed_with_ready_media = true;
            }
        }

        vTaskDelay(poll_ticks);
    }
}

fw_status_t task_field_recording_start(fw_error_context_t *error)
{
    clear_error(error);
    if (s_started) {
        return FW_STATUS_OK;
    }
    if (xTaskCreate(field_recording_task, "field_recording",
                    TASK_FIELD_RECORDING_STACK_SIZE_BYTES, NULL,
                    TASK_FIELD_RECORDING_PRIORITY, NULL) != pdPASS) {
        if (error != NULL) {
            *error = (fw_error_context_t) {
                .status = FW_STATUS_INTERNAL,
                .resource = FW_ERROR_RESOURCE_MEMORY,
                .operation = FW_ERROR_OPERATION_INITIALIZE,
                .instance = FW_ERROR_INSTANCE_NONE,
                .detail = TASK_FIELD_RECORDING_STACK_SIZE_BYTES,
            };
        }
        return FW_STATUS_INTERNAL;
    }
    s_started = true;
    return FW_STATUS_OK;
}
