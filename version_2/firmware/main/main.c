#include <stddef.h>

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


void app_main(void)
{
    fw_error_context_t error;
    if (board_init(&error) != FW_STATUS_OK) {
        stop_in_safe_state();
    }
}
