#include "card_magnetic.h"

#include <stddef.h>

static void clear_error(fw_error_context_t *error)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = FW_STATUS_OK,
            .resource = FW_ERROR_RESOURCE_NONE,
            .operation = FW_ERROR_OPERATION_NONE,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = 0U,
        };
    }
}

static fw_status_t set_error(fw_error_context_t *error,
                             fw_status_t status,
                             fw_error_operation_t operation,
                             uint32_t detail)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_GPIO_EXPANDER,
            .operation = operation,
            .instance = FW_ERROR_INSTANCE_NONE,
            .detail = detail,
        };
    }
    return status;
}

static bool request_is_valid(const card_magnetic_config_t *config,
                             card_magnetic_slot_t slot,
                             card_magnetic_operation_t operation)
{
    return config != NULL && config->set_18v_rail != NULL &&
           config->set_output != NULL && config->outputs_safe != NULL &&
           config->delay_us != NULL && config->rail_settling_us > 0U &&
           config->control_pulse_us > 0U &&
           (slot == CARD_MAGNETIC_SLOT_1 ||
            slot == CARD_MAGNETIC_SLOT_2) &&
           (operation == CARD_MAGNETIC_OPERATION_SET ||
            operation == CARD_MAGNETIC_OPERATION_RESET);
}

fw_status_t card_magnetic_pulse(
    const card_magnetic_config_t *config,
    card_magnetic_slot_t slot,
    card_magnetic_operation_t operation,
    fw_error_context_t *error)
{
    clear_error(error);
    if (!request_is_valid(config, slot, operation)) {
        return set_error(
            error, FW_STATUS_INVALID_ARGUMENT,
            FW_ERROR_OPERATION_ENABLE,
            ((uint32_t)slot << 16U) | (uint32_t)operation);
    }

    fw_status_t status = config->outputs_safe(config->context, error);
    if (status == FW_STATUS_OK) {
        status = config->set_18v_rail(
            config->context, true, error);
    }
    if (status == FW_STATUS_OK) {
        config->delay_us(config->context, config->rail_settling_us);
        status = config->set_output(
            config->context, slot, operation, true, error);
    }
    if (status == FW_STATUS_OK) {
        config->delay_us(config->context, config->control_pulse_us);
        status = config->set_output(
            config->context, slot, operation, false, error);
    }

    fw_error_context_t original_error;
    if (error != NULL) {
        original_error = *error;
    }
    const fw_status_t output_status =
        config->outputs_safe(config->context, NULL);
    const fw_status_t rail_status =
        config->set_18v_rail(config->context, false, NULL);

    if (status != FW_STATUS_OK) {
        if (error != NULL) {
            *error = original_error;
        }
        return status;
    }
    if (output_status != FW_STATUS_OK) {
        return set_error(error, output_status,
                         FW_ERROR_OPERATION_DISABLE, 0U);
    }
    if (rail_status != FW_STATUS_OK) {
        return set_error(error, rail_status,
                         FW_ERROR_OPERATION_DISABLE, 0U);
    }
    clear_error(error);
    return FW_STATUS_OK;
}
