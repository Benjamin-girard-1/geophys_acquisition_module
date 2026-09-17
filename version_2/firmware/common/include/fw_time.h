#ifndef GEOPHYS_FW_TIME_H
#define GEOPHYS_FW_TIME_H

#include <stdint.h>

#define FW_MONOTONIC_FREQUENCY_HZ UINT32_C(10000000)
#define FW_MONOTONIC_TICK_NS      UINT32_C(100)

/**
 * @brief 100 ns ticks elapsed on the per-boot monotonic clock.
 *
 * This clock never represents UTC and starts at zero during firmware boot.
 * The 64-bit type is the portable container for the ESP32-S3's 54-bit
 * hardware count.
 */
typedef uint64_t fw_monotonic_100ns_t;

#endif /* GEOPHYS_FW_TIME_H */
