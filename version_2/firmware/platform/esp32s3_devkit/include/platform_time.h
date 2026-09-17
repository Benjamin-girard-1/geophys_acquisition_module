#ifndef GEOPHYS_PLATFORM_TIME_H
#define GEOPHYS_PLATFORM_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_error.h"
#include "fw_time.h"

#define PLATFORM_MONOTONIC_COUNTER_BITS UINT32_C(54)

/**
 * @brief Initialize the per-boot 10 MHz monotonic hardware timer.
 *
 * This operation is idempotent and must succeed before either timestamp read
 * operation is used.
 */
fw_status_t platform_monotonic_time_initialize(fw_error_context_t *error);

/**
 * @brief Read the current monotonic timestamp in 100 ns ticks.
 *
 * This task-context operation reports invalid arguments, use before
 * initialization, and timer read failures through the common error model.
 */
fw_status_t platform_monotonic_time_100ns(
    fw_monotonic_100ns_t *timestamp,
    fw_error_context_t *error);

/**
 * @brief Read the same monotonic clock from interrupt context.
 *
 * This operation is non-blocking, performs no allocation, and is explicitly
 * safe for use by the ADC data-ready ISR. It returns false if the timer is not
 * initialized, timestamp is NULL, or the hardware count cannot be captured.
 */
bool platform_monotonic_time_100ns_isr(
    fw_monotonic_100ns_t *timestamp);

/** @brief Busy-wait for a short component timing interval in task context. */
void platform_delay_us(uint32_t duration_us);

/** @brief Yield the calling task for at least the requested milliseconds. */
void platform_delay_ms(uint32_t duration_ms);

#endif /* GEOPHYS_PLATFORM_TIME_H */
