#include "platform_time.h"

#include <stdbool.h>

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_error.h"
#include "soc/soc_caps.h"

_Static_assert(SOC_TIMER_GROUP_COUNTER_BIT_WIDTH ==
                   PLATFORM_MONOTONIC_COUNTER_BITS,
               "ESP32-S3 GPTimer counter width changed");

static gptimer_handle_t s_monotonic_timer;

static void delete_timer_best_effort(gptimer_handle_t timer, bool enabled)
{
    if (enabled) {
        (void)gptimer_disable(timer);
    }
    (void)gptimer_del_timer(timer);
}

fw_status_t platform_monotonic_time_initialize(fw_error_context_t *error)
{
    platform_error_clear(error);
    if (s_monotonic_timer != NULL) {
        return FW_STATUS_OK;
    }

    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_XTAL,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = FW_MONOTONIC_FREQUENCY_HZ,
    };
    gptimer_handle_t timer = NULL;
    esp_err_t result = gptimer_new_timer(&config, &timer);
    if (result != ESP_OK) {
        return platform_error_from_esp_err(
            result, error, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_INITIALIZE, FW_ERROR_INSTANCE_NONE,
            FW_MONOTONIC_FREQUENCY_HZ);
    }

    uint32_t actual_resolution_hz = 0U;
    result = gptimer_get_resolution(timer, &actual_resolution_hz);
    if (result != ESP_OK) {
        delete_timer_best_effort(timer, false);
        return platform_error_from_esp_err(
            result, error, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_GET_CLOCK, FW_ERROR_INSTANCE_NONE,
            FW_MONOTONIC_FREQUENCY_HZ);
    }
    if (actual_resolution_hz != FW_MONOTONIC_FREQUENCY_HZ) {
        delete_timer_best_effort(timer, false);
        return platform_error_set(
            error, FW_STATUS_UNSUPPORTED, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_GET_CLOCK, FW_ERROR_INSTANCE_NONE,
            actual_resolution_hz);
    }

    result = gptimer_set_raw_count(timer, 0U);
    if (result != ESP_OK) {
        delete_timer_best_effort(timer, false);
        return platform_error_from_esp_err(
            result, error, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_CONFIGURE, FW_ERROR_INSTANCE_NONE, 0U);
    }

    result = gptimer_enable(timer);
    if (result != ESP_OK) {
        delete_timer_best_effort(timer, false);
        return platform_error_from_esp_err(
            result, error, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_ENABLE, FW_ERROR_INSTANCE_NONE,
            FW_MONOTONIC_FREQUENCY_HZ);
    }

    result = gptimer_start(timer);
    if (result != ESP_OK) {
        delete_timer_best_effort(timer, true);
        return platform_error_from_esp_err(
            result, error, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_ENABLE, FW_ERROR_INSTANCE_NONE,
            FW_MONOTONIC_FREQUENCY_HZ);
    }

    s_monotonic_timer = timer;
    return FW_STATUS_OK;
}

fw_status_t platform_monotonic_time_100ns(
    fw_monotonic_100ns_t *timestamp,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (timestamp == NULL) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_READ, FW_ERROR_INSTANCE_NONE, 0U);
    }

    *timestamp = 0U;
    if (s_monotonic_timer == NULL) {
        return platform_error_set(
            error, FW_STATUS_NOT_INITIALIZED, FW_ERROR_RESOURCE_TIMER,
            FW_ERROR_OPERATION_READ, FW_ERROR_INSTANCE_NONE, 0U);
    }

    const esp_err_t result =
        gptimer_get_raw_count(s_monotonic_timer, timestamp);
    return platform_error_from_esp_err(
        result, error, FW_ERROR_RESOURCE_TIMER, FW_ERROR_OPERATION_READ,
        FW_ERROR_INSTANCE_NONE, 0U);
}

bool IRAM_ATTR platform_monotonic_time_100ns_isr(
    fw_monotonic_100ns_t *timestamp)
{
    if ((timestamp == NULL) || (s_monotonic_timer == NULL)) {
        return false;
    }

    *timestamp = 0U;
    return gptimer_get_raw_count(s_monotonic_timer, timestamp) == ESP_OK;
}

void platform_delay_us(uint32_t duration_us)
{
    esp_rom_delay_us(duration_us);
}

void platform_delay_ms(uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return;
    }

    const uint64_t ticks =
        (((uint64_t)duration_ms * (uint64_t)configTICK_RATE_HZ) +
         UINT64_C(999)) /
        UINT64_C(1000);
    vTaskDelay((ticks >= (uint64_t)portMAX_DELAY) ?
               (portMAX_DELAY - 1U) : (TickType_t)ticks);
}
