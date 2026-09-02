#ifndef GEOPHYS_BOARD_REV_1_H
#define GEOPHYS_BOARD_REV_1_H

#include <stdbool.h>

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

#endif /* GEOPHYS_BOARD_REV_1_H */
