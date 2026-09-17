#include <stddef.h>

#include "app.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_time.h"

static void stop_in_safe_state(void)
{
    (void)board_enter_safe_state(NULL);
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

void app_main(void)
{
    fw_error_context_t error;
    if (platform_monotonic_time_initialize(&error) != FW_STATUS_OK) {
        stop_in_safe_state();
    }

    if (board_init(&error) != FW_STATUS_OK) {
        stop_in_safe_state();
    }

    if (app_start(&error) != FW_STATUS_OK) {
        stop_in_safe_state();
    }
}
