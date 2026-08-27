#ifndef GEOPHYS_PLATFORM_TIME_H
#define GEOPHYS_PLATFORM_TIME_H

#include "fw_time.h"

/**
 * @brief Return microseconds elapsed on the current boot's monotonic clock.
 *
 * This task-context operation is available when ESP-IDF calls app_main().
 */
fw_monotonic_us_t platform_monotonic_time_us(void);

/**
 * @brief Return the same monotonic clock from interrupt context.
 *
 * This operation is non-blocking, performs no allocation, and is explicitly
 * safe for use by the ADC data-ready ISR.
 */
fw_monotonic_us_t platform_monotonic_time_us_isr(void);

#endif /* GEOPHYS_PLATFORM_TIME_H */
