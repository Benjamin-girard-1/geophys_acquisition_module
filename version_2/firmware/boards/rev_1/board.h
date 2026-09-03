#ifndef GEOPHYS_BOARD_REV_1_H
#define GEOPHYS_BOARD_REV_1_H

#include <stdbool.h>
#include <stdint.h>

#include "ad7779.h"
#include "fw_error.h"

typedef enum {
    BOARD_POWER_RAIL_3V3A = 0,
    BOARD_POWER_RAIL_10V,
    BOARD_POWER_RAIL_NEGATIVE_5V,
    BOARD_POWER_RAIL_18V,
} board_power_rail_t;

/**
 * @brief Establish direct safe GPIO states and apply the complete safe image.
 *
 * The shift-register outputs remain disabled until all 16 safe bits have been
 * latched. GPIO42 is deliberately left high impedance.
 */
fw_status_t board_init(fw_error_context_t *error);

/** @brief Restore the complete Rev-1 safe image. */
fw_status_t board_enter_safe_state(fw_error_context_t *error);

/**
 * @brief Change one board-owned rail while preserving every unrelated bit.
 *
 * This mechanism does not implement product power policy or required delays.
 * Its caller must apply the ordering and settling rules from the board
 * contract. The 18 V rail is permitted only for an explicit pulse or bench
 * diagnostic operation.
 */
fw_status_t board_set_power_rail(board_power_rail_t rail,
                                 bool enabled,
                                 fw_error_context_t *error);

/**
 * @brief Create the Rev-1 SPI2 connection and initialize one AD7779 instance.
 *
 * The caller must enable and settle the required analog rails first. The
 * caller remains the sole owner of adc; the board supplies only Rev-1 wiring,
 * SPI, control callbacks, and timing values.
 */
fw_status_t board_adc_initialize(ad7779_t *adc,
                                 fw_error_context_t *error);

/** @brief Return the requested and achieved Rev-1 AD7779 SPI clock. */
fw_status_t board_adc_get_spi_clock(uint32_t *requested_clock_hz,
                                    uint32_t *actual_clock_hz,
                                    fw_error_context_t *error);

/** @brief Stop the ADC and release its Rev-1 SPI resources. */
fw_status_t board_adc_deinitialize(ad7779_t *adc,
                                   fw_error_context_t *error);

#endif /* GEOPHYS_BOARD_REV_1_H */
