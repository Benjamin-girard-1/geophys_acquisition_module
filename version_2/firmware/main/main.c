#include <stddef.h>

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RAIL_BENCH_STEP_MS 1000U

static void wait_for_oscilloscope_step(void)
{
    vTaskDelay(pdMS_TO_TICKS(RAIL_BENCH_STEP_MS));
}

static void stop_in_safe_state(void)
{
    (void)board_enter_safe_state(NULL);
    for (;;) {
        wait_for_oscilloscope_step();
    }
}

void app_main(void)
{
    fw_error_context_t error;
    if (board_init(&error) != FW_STATUS_OK) {
        stop_in_safe_state();
    }

    /*
     * TEMPORARY FW-14/FW-18 OSCILLOSCOPE DIAGNOSTIC.
     *
     * Rails turn on cumulatively in the documented startup order, including
     * an explicit 18 V bench request. Once all rails are enabled, they remain
     * enabled until reset or power removal. No magnetic SET or RESET output is
     * asserted.
     */
    const board_power_rail_t rail_sequence[] = {
        BOARD_POWER_RAIL_3V3A,
        BOARD_POWER_RAIL_10V,
        BOARD_POWER_RAIL_NEGATIVE_5V,
        BOARD_POWER_RAIL_18V,
    };

    wait_for_oscilloscope_step();

    for (size_t index = 0U;
         index < (sizeof(rail_sequence) / sizeof(rail_sequence[0]));
         index++) {
        if (board_set_power_rail(
                rail_sequence[index], true, &error) != FW_STATUS_OK) {
            stop_in_safe_state();
        }
        wait_for_oscilloscope_step();
    }

    for (;;) {
        wait_for_oscilloscope_step();
    }
}
