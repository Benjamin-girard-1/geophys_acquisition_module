#include "platform_analog_input.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "platform_error.h"

#if !ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
#error "ESP32-S3 analog inputs require ADC curve-fitting calibration"
#endif

typedef struct {
    platform_gpio_pin_t pin;
    adc_channel_t channel;
    adc_cali_handle_t calibration;
} platform_analog_input_channel_t;

struct platform_analog_input_bank {
    adc_unit_t unit;
    adc_oneshot_unit_handle_t unit_handle;
    size_t input_count;
    bool initialized;
    platform_analog_input_channel_t inputs[];
};

static uint32_t size_to_detail(size_t size)
{
    return (size > (size_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)size;
}

static uint32_t bank_instance(const platform_analog_input_bank_t *bank)
{
    if ((bank == NULL) || (bank->input_count == 0U)) {
        return FW_ERROR_INSTANCE_NONE;
    }
    return bank->inputs[0].pin;
}

static bool range_to_attenuation(platform_analog_input_range_t range,
                                 adc_atten_t *attenuation)
{
    if (range != PLATFORM_ANALOG_INPUT_RANGE_3V3) {
        return false;
    }

    *attenuation = ADC_ATTEN_DB_12;
    return true;
}

static void release_bank_best_effort(platform_analog_input_bank_t *bank)
{
    if (bank == NULL) {
        return;
    }

    for (size_t index = 0U; index < bank->input_count; index++) {
        if (bank->inputs[index].calibration != NULL) {
            (void)adc_cali_delete_scheme_curve_fitting(
                bank->inputs[index].calibration);
            bank->inputs[index].calibration = NULL;
        }
    }

    if (bank->unit_handle != NULL) {
        (void)adc_oneshot_del_unit(bank->unit_handle);
        bank->unit_handle = NULL;
    }
    heap_caps_free(bank);
}

fw_status_t platform_analog_input_bank_initialize(
    const platform_analog_input_bank_config_t *config,
    platform_analog_input_bank_t **bank,
    fw_error_context_t *error)
{
    adc_atten_t attenuation;

    platform_error_clear(error);
    if ((config == NULL) || (bank == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_ADC,
            FW_ERROR_OPERATION_INITIALIZE, FW_ERROR_INSTANCE_NONE, 0U);
    }
    *bank = NULL;

    if ((config->pins == NULL) || (config->input_count == 0U) ||
        !range_to_attenuation(config->range, &attenuation) ||
        (config->input_count >
         ((SIZE_MAX - sizeof(platform_analog_input_bank_t)) /
          sizeof(platform_analog_input_channel_t)))) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_ADC,
            FW_ERROR_OPERATION_INITIALIZE, FW_ERROR_INSTANCE_NONE,
            size_to_detail(config->input_count));
    }

    const size_t allocation_size =
        sizeof(platform_analog_input_bank_t) +
        (config->input_count * sizeof(platform_analog_input_channel_t));
    platform_analog_input_bank_t *new_bank = heap_caps_calloc(
        1U, allocation_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (new_bank == NULL) {
        return platform_error_set(
            error, FW_STATUS_INTERNAL, FW_ERROR_RESOURCE_MEMORY,
            FW_ERROR_OPERATION_INITIALIZE, FW_ERROR_INSTANCE_NONE,
            size_to_detail(allocation_size));
    }
    new_bank->input_count = config->input_count;

    for (size_t index = 0U; index < config->input_count; index++) {
        if (config->pins[index] > (platform_gpio_pin_t)INT_MAX) {
            release_bank_best_effort(new_bank);
            return platform_error_set(
                error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_ADC,
                FW_ERROR_OPERATION_INITIALIZE, config->pins[index],
                size_to_detail(index));
        }

        adc_unit_t unit;
        adc_channel_t channel;
        fw_status_t status = platform_error_from_esp_err(
            adc_oneshot_io_to_channel((int)config->pins[index],
                                      &unit, &channel),
            error, FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_INITIALIZE,
            config->pins[index], size_to_detail(index));
        if (status != FW_STATUS_OK) {
            release_bank_best_effort(new_bank);
            return status;
        }

        if ((index > 0U) && (unit != new_bank->unit)) {
            release_bank_best_effort(new_bank);
            return platform_error_set(
                error, FW_STATUS_UNSUPPORTED, FW_ERROR_RESOURCE_ADC,
                FW_ERROR_OPERATION_INITIALIZE, config->pins[index],
                size_to_detail(index));
        }
        for (size_t previous = 0U; previous < index; previous++) {
            if (channel == new_bank->inputs[previous].channel) {
                release_bank_best_effort(new_bank);
                return platform_error_set(
                    error, FW_STATUS_INVALID_ARGUMENT,
                    FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_INITIALIZE,
                    config->pins[index], size_to_detail(index));
            }
        }

        new_bank->unit = unit;
        new_bank->inputs[index].pin = config->pins[index];
        new_bank->inputs[index].channel = channel;
    }

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = new_bank->unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    fw_status_t status = platform_error_from_esp_err(
        adc_oneshot_new_unit(&unit_config, &new_bank->unit_handle),
        error, FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_INITIALIZE,
        bank_instance(new_bank), size_to_detail(config->input_count));
    if (status != FW_STATUS_OK) {
        release_bank_best_effort(new_bank);
        return status;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    for (size_t index = 0U; index < config->input_count; index++) {
        status = platform_error_from_esp_err(
            adc_oneshot_config_channel(
                new_bank->unit_handle,
                new_bank->inputs[index].channel,
                &channel_config),
            error, FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_CONFIGURE,
            new_bank->inputs[index].pin, size_to_detail(index));
        if (status != FW_STATUS_OK) {
            release_bank_best_effort(new_bank);
            return status;
        }

        const adc_cali_curve_fitting_config_t calibration_config = {
            .unit_id = new_bank->unit,
            .chan = new_bank->inputs[index].channel,
            .atten = attenuation,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        status = platform_error_from_esp_err(
            adc_cali_create_scheme_curve_fitting(
                &calibration_config,
                &new_bank->inputs[index].calibration),
            error, FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_INITIALIZE,
            new_bank->inputs[index].pin, size_to_detail(index));
        if (status != FW_STATUS_OK) {
            release_bank_best_effort(new_bank);
            return status;
        }
    }

    new_bank->initialized = true;
    *bank = new_bank;
    return FW_STATUS_OK;
}

fw_status_t platform_analog_input_read_mv(
    platform_analog_input_bank_t *bank,
    size_t input_index,
    uint32_t *millivolts,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if ((bank == NULL) || (millivolts == NULL) ||
        (input_index >= ((bank == NULL) ? 0U : bank->input_count))) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_ADC,
            FW_ERROR_OPERATION_READ, bank_instance(bank),
            size_to_detail(input_index));
    }
    *millivolts = 0U;

    if (!bank->initialized || (bank->unit_handle == NULL) ||
        (bank->inputs[input_index].calibration == NULL)) {
        return platform_error_set(
            error, FW_STATUS_NOT_INITIALIZED, FW_ERROR_RESOURCE_ADC,
            FW_ERROR_OPERATION_READ, bank->inputs[input_index].pin,
            size_to_detail(input_index));
    }

    int raw = 0;
    fw_status_t status = platform_error_from_esp_err(
        adc_oneshot_read(bank->unit_handle,
                         bank->inputs[input_index].channel, &raw),
        error, FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_READ,
        bank->inputs[input_index].pin, size_to_detail(input_index));
    if (status != FW_STATUS_OK) {
        return status;
    }

    int calibrated_mv = 0;
    status = platform_error_from_esp_err(
        adc_cali_raw_to_voltage(bank->inputs[input_index].calibration,
                                raw, &calibrated_mv),
        error, FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_READ,
        bank->inputs[input_index].pin,
        (raw < 0) ? 0U : (uint32_t)raw);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (calibrated_mv < 0) {
        return platform_error_set(
            error, FW_STATUS_HARDWARE_FAULT, FW_ERROR_RESOURCE_ADC,
            FW_ERROR_OPERATION_READ, bank->inputs[input_index].pin, 0U);
    }

    *millivolts = (uint32_t)calibrated_mv;
    return FW_STATUS_OK;
}

fw_status_t platform_analog_input_bank_deinitialize(
    platform_analog_input_bank_t *bank,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if ((bank == NULL) || !bank->initialized) {
        return platform_error_set(
            error, FW_STATUS_NOT_INITIALIZED, FW_ERROR_RESOURCE_ADC,
            FW_ERROR_OPERATION_DEINITIALIZE, bank_instance(bank), 0U);
    }

    for (size_t index = 0U; index < bank->input_count; index++) {
        if (bank->inputs[index].calibration == NULL) {
            continue;
        }
        const fw_status_t status = platform_error_from_esp_err(
            adc_cali_delete_scheme_curve_fitting(
                bank->inputs[index].calibration),
            error, FW_ERROR_RESOURCE_ADC,
            FW_ERROR_OPERATION_DEINITIALIZE,
            bank->inputs[index].pin, size_to_detail(index));
        if (status != FW_STATUS_OK) {
            return status;
        }
        bank->inputs[index].calibration = NULL;
    }

    const fw_status_t status = platform_error_from_esp_err(
        adc_oneshot_del_unit(bank->unit_handle),
        error, FW_ERROR_RESOURCE_ADC, FW_ERROR_OPERATION_DEINITIALIZE,
        bank_instance(bank), 0U);
    if (status != FW_STATUS_OK) {
        return status;
    }

    bank->unit_handle = NULL;
    bank->initialized = false;
    heap_caps_free(bank);
    return FW_STATUS_OK;
}
