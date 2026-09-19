#include "adc_scope_test.h"

#include "ad7779.h"
#include "board.h"

static ad7779_t s_scope_adc;

fw_status_t adc_scope_test_start(fw_error_context_t *error)
{
    fw_status_t status = board_adc_power_up(error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = board_adc_initialize(&s_scope_adc, error);
    if (status != FW_STATUS_OK) {
        (void)board_adc_power_down(NULL);
        return status;
    }

    status = ad7779_configure_default_channels(&s_scope_adc, error);
    if (status == FW_STATUS_OK) {
        status = ad7779_configure_output_rate(
            &s_scope_adc, AD7779_OUTPUT_RATE_1000_SPS, error);
    }
    if (status == FW_STATUS_OK) {
        status = ad7779_start(&s_scope_adc, error);
    }
    if (status != FW_STATUS_OK) {
        (void)board_adc_deinitialize(&s_scope_adc, NULL);
        (void)board_adc_power_down(NULL);
    }
    return status;
}
