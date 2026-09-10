#ifndef GEOPHYS_CARD_DETECT_H
#define GEOPHYS_CARD_DETECT_H

#include <stdint.h>

#include "fw_error.h"

#define CARD_DETECT_MAX_SAMPLE_COUNT UINT16_C(256)

/** @brief Read one already-calibrated analog-ID value in millivolts. */
typedef fw_status_t (*card_detect_read_mv_callback_t)(
    void *context,
    uint32_t *millivolts,
    fw_error_context_t *error);

/** @brief Wait between samples without prescribing an RTOS or timer. */
typedef void (*card_detect_delay_us_callback_t)(void *context,
                                                uint32_t duration_us);

typedef struct {
    card_detect_read_mv_callback_t read_mv;
    card_detect_delay_us_callback_t delay_us;
    void *read_context;
    void *delay_context;
    uint16_t sample_count;
    uint32_t sample_interval_us;
    uint32_t instance;
} card_detect_measure_config_t;

/**
 * @brief Aggregated result from one bounded analog-ID sampling window.
 *
 * median_mv is the robust value intended for later card classification.
 * average_mv is retained for diagnostics and comparison. No classification is
 * performed by this FW-27 interface.
 */
typedef struct {
    uint32_t average_mv;
    uint32_t median_mv;
    uint32_t minimum_mv;
    uint32_t maximum_mv;
    uint16_t sample_count;
} card_detect_measurement_t;

/**
 * @brief Sample and aggregate one analog-card identification voltage.
 *
 * The caller supplies voltage and delay callbacks, keeping MCU ADC and RTOS
 * details outside this portable card module.
 */
fw_status_t card_detect_measure(
    const card_detect_measure_config_t *config,
    card_detect_measurement_t *measurement,
    fw_error_context_t *error);

#endif /* GEOPHYS_CARD_DETECT_H */
