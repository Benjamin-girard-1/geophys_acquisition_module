#ifndef GEOPHYS_PLATFORM_ANALOG_INPUT_H
#define GEOPHYS_PLATFORM_ANALOG_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "fw_error.h"
#include "platform_gpio.h"

typedef struct platform_analog_input_bank platform_analog_input_bank_t;

/**
 * @brief Logical input-voltage ranges supported by this platform.
 *
 * The platform selects the MCU-specific attenuation and calibration scheme.
 * Callers never handle ESP32 ADC units, channels, raw codes, or calibration
 * objects.
 */
typedef enum {
    PLATFORM_ANALOG_INPUT_RANGE_3V3 = 0,
} platform_analog_input_range_t;

typedef struct {
    const platform_gpio_pin_t *pins;
    size_t input_count;
    platform_analog_input_range_t range;
} platform_analog_input_bank_config_t;

/**
 * @brief Configure a calibrated bank of analog inputs.
 *
 * Every pin in one bank must belong to the same MCU ADC unit. Initialization
 * may allocate memory; reads do not allocate.
 */
fw_status_t platform_analog_input_bank_initialize(
    const platform_analog_input_bank_config_t *config,
    platform_analog_input_bank_t **bank,
    fw_error_context_t *error);

/** @brief Read one input and return its calibrated voltage in millivolts. */
fw_status_t platform_analog_input_read_mv(
    platform_analog_input_bank_t *bank,
    size_t input_index,
    uint32_t *millivolts,
    fw_error_context_t *error);

/** @brief Release a calibrated analog-input bank. */
fw_status_t platform_analog_input_bank_deinitialize(
    platform_analog_input_bank_t *bank,
    fw_error_context_t *error);

#endif /* GEOPHYS_PLATFORM_ANALOG_INPUT_H */
