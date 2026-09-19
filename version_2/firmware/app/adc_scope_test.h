#ifndef GEOPHYS_ADC_SCOPE_TEST_H
#define GEOPHYS_ADC_SCOPE_TEST_H

#include "fw_error.h"

/** Start the bench-only AD7779 clock/DRDY scope mode. */
fw_status_t adc_scope_test_start(fw_error_context_t *error);

#endif /* GEOPHYS_ADC_SCOPE_TEST_H */
