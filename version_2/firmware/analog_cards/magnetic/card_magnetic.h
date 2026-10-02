#ifndef GEOPHYS_CARD_MAGNETIC_H
#define GEOPHYS_CARD_MAGNETIC_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_error.h"

#define CARD_MAGNETIC_RAIL_SETTLING_US UINT32_C(100000)
#define CARD_MAGNETIC_CONTROL_PULSE_US UINT32_C(200)

typedef enum {
    CARD_MAGNETIC_SLOT_INVALID = 0,
    CARD_MAGNETIC_SLOT_1,
    CARD_MAGNETIC_SLOT_2,
} card_magnetic_slot_t;

typedef enum {
    CARD_MAGNETIC_OPERATION_INVALID = 0,
    CARD_MAGNETIC_OPERATION_SET,
    CARD_MAGNETIC_OPERATION_RESET,
} card_magnetic_operation_t;

typedef fw_status_t (*card_magnetic_set_rail_callback_t)(
    void *context,
    bool enabled,
    fw_error_context_t *error);

typedef fw_status_t (*card_magnetic_set_output_callback_t)(
    void *context,
    card_magnetic_slot_t slot,
    card_magnetic_operation_t operation,
    bool enabled,
    fw_error_context_t *error);

typedef fw_status_t (*card_magnetic_outputs_safe_callback_t)(
    void *context,
    fw_error_context_t *error);

typedef void (*card_magnetic_delay_us_callback_t)(
    void *context,
    uint32_t duration_us);

typedef struct {
    card_magnetic_set_rail_callback_t set_18v_rail;
    card_magnetic_set_output_callback_t set_output;
    card_magnetic_outputs_safe_callback_t outputs_safe;
    card_magnetic_delay_us_callback_t delay_us;
    void *context;
    uint32_t rail_settling_us;
    uint32_t control_pulse_us;
} card_magnetic_config_t;

/** Execute one independent SET or RESET pulse and always restore safe outputs. */
fw_status_t card_magnetic_pulse(
    const card_magnetic_config_t *config,
    card_magnetic_slot_t slot,
    card_magnetic_operation_t operation,
    fw_error_context_t *error);

#endif /* GEOPHYS_CARD_MAGNETIC_H */
