#include <stddef.h>

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    if (board_init(&error) != FW_STATUS_OK) {
        stop_in_safe_state();
    }
}
