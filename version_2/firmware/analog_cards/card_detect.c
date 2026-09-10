#include "card_detect.h"

#include <stddef.h>
#include <stdint.h>

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
                             uint32_t instance,
                             uint32_t detail)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_ADC,
            .operation = FW_ERROR_OPERATION_READ,
            .instance = instance,
            .detail = detail,
        };
    }
    return status;
}

static void clear_measurement(card_detect_measurement_t *measurement)
{
    if (measurement != NULL) {
        *measurement = (card_detect_measurement_t) {0};
    }
}

static void insertion_sort(uint16_t *samples, uint16_t sample_count)
{
    for (uint16_t index = 1U; index < sample_count; index++) {
        const uint16_t value = samples[index];
        uint16_t position = index;
        while ((position > 0U) &&
               (samples[position - 1U] > value)) {
            samples[position] = samples[position - 1U];
            position--;
        }
        samples[position] = value;
    }
}

fw_status_t card_detect_measure(
    const card_detect_measure_config_t *config,
    card_detect_measurement_t *measurement,
    fw_error_context_t *error)
{
    clear_error(error);
    clear_measurement(measurement);

    const uint32_t instance = (config == NULL) ?
        FW_ERROR_INSTANCE_NONE : config->instance;
    if ((config == NULL) || (measurement == NULL) ||
        (config->read_mv == NULL) || (config->sample_count == 0U) ||
        (config->sample_count > CARD_DETECT_MAX_SAMPLE_COUNT) ||
        ((config->sample_interval_us > 0U) &&
         (config->delay_us == NULL))) {
        return set_error(
            error, FW_STATUS_INVALID_ARGUMENT, instance,
            (config == NULL) ? 0U : config->sample_count);
    }

    uint16_t samples_mv[CARD_DETECT_MAX_SAMPLE_COUNT];
    uint64_t sum_mv = 0U;
    for (uint16_t index = 0U; index < config->sample_count; index++) {
        uint32_t sample_mv = 0U;
        const fw_status_t status = config->read_mv(
            config->read_context, &sample_mv, error);
        if (status != FW_STATUS_OK) {
            clear_measurement(measurement);
            if ((error != NULL) && (error->status == FW_STATUS_OK)) {
                (void)set_error(error, status, instance, index);
            }
            return status;
        }
        if (sample_mv > UINT16_MAX) {
            clear_measurement(measurement);
            return set_error(
                error, FW_STATUS_OVERFLOW, instance, sample_mv);
        }

        samples_mv[index] = (uint16_t)sample_mv;
        sum_mv += sample_mv;

        if (((uint16_t)(index + 1U) < config->sample_count) &&
            (config->sample_interval_us > 0U)) {
            config->delay_us(
                config->delay_context, config->sample_interval_us);
        }
    }

    insertion_sort(samples_mv, config->sample_count);

    const uint16_t midpoint = config->sample_count / 2U;
    uint32_t median_mv = samples_mv[midpoint];
    if ((config->sample_count % 2U) == 0U) {
        median_mv =
            ((uint32_t)samples_mv[midpoint - 1U] +
             (uint32_t)samples_mv[midpoint] + 1U) /
            2U;
    }

    measurement->average_mv =
        (uint32_t)((sum_mv + (config->sample_count / 2U)) /
                   config->sample_count);
    measurement->median_mv = median_mv;
    measurement->minimum_mv = samples_mv[0];
    measurement->maximum_mv = samples_mv[config->sample_count - 1U];
    measurement->sample_count = config->sample_count;
    return FW_STATUS_OK;
}
