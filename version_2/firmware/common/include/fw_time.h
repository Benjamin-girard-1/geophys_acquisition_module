#ifndef GEOPHYS_FW_TIME_H
#define GEOPHYS_FW_TIME_H

#include <stdint.h>

/**
 * @brief Microseconds elapsed on the per-boot monotonic clock.
 *
 * This clock never represents UTC and restarts when the firmware boots.
 */
typedef uint64_t fw_monotonic_us_t;

#endif /* GEOPHYS_FW_TIME_H */
