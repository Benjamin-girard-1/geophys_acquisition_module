#include "board.h"

#include <stddef.h>
#include <stdint.h>

#include "74hc_hct595.h"
#include "board_config.h"
#include "platform_gpio.h"
#include "platform_spi.h"
#include "platform_time.h"

static hc595_t s_shift_register;
static platform_spi_bus_t *s_adc_spi_bus;
static platform_spi_device_t *s_adc_spi_device;
static ad7779_t *s_adc_owner;
static bool s_shift_gpio_configured;
static bool s_board_initialized;

_Static_assert(HC595_OUTPUT_COUNT == 16U,
               "Rev-1 requires exactly two daisy-chained 74HC595 devices");
_Static_assert(
    BOARD_REV1_SHIFT_SAFE_IMAGE ==
        BOARD_REV1_SHIFT_MASK(BOARD_REV1_SHIFT_ADC_START),
    "Rev-1 safe image must contain only inactive-high ADC_START");
_Static_assert(
    (BOARD_REV1_SHIFT_SAFE_IMAGE & BOARD_REV1_SHIFT_FIXED_LOW_MASK) == 0U,
    "Rev-1 safe image must keep SD/USB fixed outputs low");

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

static fw_status_t set_board_error(fw_error_context_t *error,
                                   fw_status_t status,
                                   fw_error_operation_t operation,
                                   uint32_t detail)
{
    if (error != NULL) {
        *error = (fw_error_context_t) {
            .status = status,
            .resource = FW_ERROR_RESOURCE_GPIO_EXPANDER,
            .operation = operation,
            .instance = BOARD_REV1_SR_INSTANCE,
            .detail = detail,
        };
    }
    return status;
}

static platform_gpio_pin_t shift_line_pin(hc595_line_t line)
{
    switch (line) {
    case HC595_LINE_SERIAL_DATA:
        return BOARD_REV1_GPIO_SR_SERIAL_DATA;
    case HC595_LINE_SHIFT_CLOCK:
        return BOARD_REV1_GPIO_SR_SHIFT_CLOCK;
    case HC595_LINE_STORAGE_CLOCK:
        return BOARD_REV1_GPIO_SR_STORAGE_CLOCK;
    case HC595_LINE_OUTPUT_ENABLE_N:
        return BOARD_REV1_GPIO_SR_OUTPUT_ENABLE_N;
    default:
        return UINT32_MAX;
    }
}

static fw_status_t write_shift_line(void *context,
                                    hc595_line_t line,
                                    bool high,
                                    fw_error_context_t *error)
{
    (void)context;
    const platform_gpio_pin_t pin = shift_line_pin(line);
    if (pin == UINT32_MAX) {
        return set_board_error(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_OPERATION_WRITE,
            (uint32_t)line);
    }

    return platform_gpio_write(
        pin,
        high ? PLATFORM_GPIO_LEVEL_HIGH : PLATFORM_GPIO_LEVEL_LOW,
        error);
}

static void delay_shift_edge(void *context, uint32_t duration_us)
{
    (void)context;
    platform_delay_us(duration_us);
}

static fw_status_t configure_output(platform_gpio_pin_t pin,
                                    platform_gpio_level_t safe_level,
                                    fw_error_context_t *error)
{
    return platform_gpio_configure_output(pin, safe_level, error);
}

static fw_status_t configure_input(platform_gpio_pin_t pin,
                                   fw_error_context_t *error)
{
    return platform_gpio_configure_input(
        pin, PLATFORM_GPIO_PULL_NONE, error);
}

static void disable_shift_outputs_best_effort(void)
{
    if (s_shift_gpio_configured) {
        (void)platform_gpio_write(
            BOARD_REV1_GPIO_SR_OUTPUT_ENABLE_N,
            BOARD_REV1_SR_OUTPUT_DISABLE_LEVEL,
            NULL);
    }
}

static fw_status_t initialize_direct_gpio(fw_error_context_t *error)
{
    fw_status_t status = configure_output(
        BOARD_REV1_GPIO_ADC_CS, BOARD_REV1_ADC_CS_SAFE_LEVEL, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = configure_output(
        BOARD_REV1_GPIO_SR_OUTPUT_ENABLE_N,
        BOARD_REV1_SR_OUTPUT_DISABLE_LEVEL,
        error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    s_shift_gpio_configured = true;

    status = configure_output(
        BOARD_REV1_GPIO_SR_SHIFT_CLOCK,
        BOARD_REV1_SR_SHIFT_SAFE_LEVEL,
        error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = configure_output(
        BOARD_REV1_GPIO_SR_SERIAL_DATA,
        BOARD_REV1_SR_DATA_SAFE_LEVEL,
        error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = configure_output(
        BOARD_REV1_GPIO_SR_STORAGE_CLOCK,
        BOARD_REV1_SR_STORAGE_SAFE_LEVEL,
        error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    const platform_gpio_pin_t safe_inputs[] = {
        BOARD_REV1_GPIO_ADC_DRDY,
        BOARD_REV1_GPIO_DEVICE_DETECT_1,
        BOARD_REV1_GPIO_DEVICE_DETECT_2,
        BOARD_REV1_GPIO_SOLAR_PRESENT,
        BOARD_REV1_GPIO_USB_5V_PRESENT,
        BOARD_REV1_GPIO_SUPERCAP_CHARGE_ENABLE,
    };

    for (size_t index = 0U;
         index < (sizeof(safe_inputs) / sizeof(safe_inputs[0]));
         index++) {
        status = configure_input(safe_inputs[index], error);
        if (status != FW_STATUS_OK) {
            return status;
        }
    }

    return FW_STATUS_OK;
}

fw_status_t board_init(fw_error_context_t *error)
{
    clear_error(error);

    if (s_board_initialized) {
        return FW_STATUS_OK;
    }

    fw_status_t status = initialize_direct_gpio(error);
    if (status != FW_STATUS_OK) {
        disable_shift_outputs_best_effort();
        return status;
    }

    const hc595_config_t shift_config = {
        .write_line = write_shift_line,
        .delay_us = delay_shift_edge,
        .context = NULL,
        .edge_delay_us = BOARD_REV1_SR_EDGE_DELAY_US,
        .instance = BOARD_REV1_SR_INSTANCE,
    };

    status = hc595_initialize(
        &s_shift_register,
        &shift_config,
        BOARD_REV1_SHIFT_SAFE_IMAGE,
        error);
    if (status != FW_STATUS_OK) {
        disable_shift_outputs_best_effort();
        return status;
    }

    status = hc595_set_outputs_enabled(&s_shift_register, true, error);
    if (status != FW_STATUS_OK) {
        disable_shift_outputs_best_effort();
        return status;
    }

    s_board_initialized = true;
    return FW_STATUS_OK;
}

fw_status_t board_enter_safe_state(fw_error_context_t *error)
{
    clear_error(error);

    if (!s_shift_gpio_configured || !s_shift_register.initialized) {
        disable_shift_outputs_best_effort();
        return set_board_error(
            error, FW_STATUS_NOT_INITIALIZED,
            FW_ERROR_OPERATION_DISABLE,
            BOARD_REV1_SHIFT_SAFE_IMAGE);
    }

    /*
     * Keep outputs enabled here. The storage register retains the current
     * image while new bits shift, then all outputs change together at STCP.
     * Disabling OE would expose resistor-defined states that are not safe.
     */
    fw_status_t status = hc595_write_image(
        &s_shift_register, BOARD_REV1_SHIFT_SAFE_IMAGE, error);
    if (status != FW_STATUS_OK) {
        disable_shift_outputs_best_effort();
        return status;
    }

    status = platform_gpio_write(
        BOARD_REV1_GPIO_ADC_CS, BOARD_REV1_ADC_CS_SAFE_LEVEL, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    return FW_STATUS_OK;
}

static bool power_rail_output(board_power_rail_t rail,
                              board_rev1_shift_output_t *output)
{
    switch (rail) {
    case BOARD_POWER_RAIL_3V3A:
        *output = BOARD_REV1_SHIFT_3V3A_ENABLE;
        return true;
    case BOARD_POWER_RAIL_10V:
        *output = BOARD_REV1_SHIFT_10V_ENABLE;
        return true;
    case BOARD_POWER_RAIL_NEGATIVE_5V:
        *output = BOARD_REV1_SHIFT_NEGATIVE_5V_ENABLE;
        return true;
    case BOARD_POWER_RAIL_18V:
        *output = BOARD_REV1_SHIFT_18V_ENABLE;
        return true;
    default:
        return false;
    }
}

fw_status_t board_set_power_rail(board_power_rail_t rail,
                                 bool enabled,
                                 fw_error_context_t *error)
{
    clear_error(error);

    if (!s_board_initialized) {
        return set_board_error(
            error, FW_STATUS_NOT_INITIALIZED,
            enabled ? FW_ERROR_OPERATION_ENABLE : FW_ERROR_OPERATION_DISABLE,
            (uint32_t)rail);
    }

    board_rev1_shift_output_t output;
    if (!power_rail_output(rail, &output)) {
        return set_board_error(
            error, FW_STATUS_INVALID_ARGUMENT,
            enabled ? FW_ERROR_OPERATION_ENABLE : FW_ERROR_OPERATION_DISABLE,
            (uint32_t)rail);
    }

    return hc595_set_output(
        &s_shift_register, (uint8_t)output, enabled, error);
}

static fw_status_t set_adc_shift_output(board_rev1_shift_output_t output,
                                        bool high,
                                        fw_error_context_t *error)
{
    if (!s_board_initialized) {
        return set_board_error(error, FW_STATUS_NOT_INITIALIZED,
                               FW_ERROR_OPERATION_WRITE,
                               (uint32_t)output);
    }
    return hc595_set_output(&s_shift_register, (uint8_t)output, high, error);
}

static fw_status_t set_adc_mclk_enabled(void *context,
                                        bool enabled,
                                        fw_error_context_t *error)
{
    (void)context;
    const bool high = enabled ? BOARD_REV1_ADC_MCLK_ENABLE_ACTIVE_LEVEL
                              : BOARD_REV1_ADC_MCLK_ENABLE_SAFE_LEVEL;
    return set_adc_shift_output(BOARD_REV1_SHIFT_ADC_MCLK_ENABLE, high,
                                error);
}

static fw_status_t set_adc_reset_asserted(void *context,
                                          bool asserted,
                                          fw_error_context_t *error)
{
    (void)context;
    const bool high = asserted ? BOARD_REV1_ADC_RESET_ACTIVE_LEVEL
                               : !BOARD_REV1_ADC_RESET_ACTIVE_LEVEL;
    return set_adc_shift_output(BOARD_REV1_SHIFT_ADC_RESET, high, error);
}

static fw_status_t set_adc_start_high(void *context,
                                      bool high,
                                      fw_error_context_t *error)
{
    (void)context;
    return set_adc_shift_output(BOARD_REV1_SHIFT_ADC_START, high, error);
}

static void delay_adc_control(void *context, uint32_t duration_us)
{
    (void)context;
    platform_delay_us(duration_us);
}

static void release_adc_spi_best_effort(void)
{
    if (s_adc_spi_device != NULL) {
        (void)platform_spi_device_remove(
            s_adc_spi_device, BOARD_REV1_ADC_SPI_TRANSFER_TIMEOUT_US, NULL);
        s_adc_spi_device = NULL;
    }
    if (s_adc_spi_bus != NULL) {
        (void)platform_spi_bus_deinitialize(
            s_adc_spi_bus, BOARD_REV1_ADC_SPI_TRANSFER_TIMEOUT_US, NULL);
        s_adc_spi_bus = NULL;
    }
    s_adc_owner = NULL;
}

fw_status_t board_adc_initialize(ad7779_t *adc,
                                 fw_error_context_t *error)
{
    clear_error(error);
    if (!s_board_initialized) {
        return set_board_error(error, FW_STATUS_NOT_INITIALIZED,
                               FW_ERROR_OPERATION_INITIALIZE,
                               BOARD_REV1_ADC_INSTANCE);
    }
    if (adc == NULL) {
        return set_board_error(error, FW_STATUS_INVALID_ARGUMENT,
                               FW_ERROR_OPERATION_INITIALIZE,
                               BOARD_REV1_ADC_INSTANCE);
    }
    if (s_adc_spi_bus != NULL || s_adc_spi_device != NULL ||
        s_adc_owner != NULL) {
        return set_board_error(error, FW_STATUS_INVALID_STATE,
                               FW_ERROR_OPERATION_INITIALIZE,
                               BOARD_REV1_ADC_INSTANCE);
    }

    const platform_spi_bus_config_t bus_config = {
        .host = BOARD_REV1_ADC_SPI_HOST,
        .clock_pin = BOARD_REV1_GPIO_ADC_SCLK,
        .mosi_pin = BOARD_REV1_GPIO_ADC_MOSI,
        .miso_pin = BOARD_REV1_GPIO_ADC_MISO,
        .maximum_transfer_size_bytes =
            BOARD_REV1_ADC_SPI_MAX_TRANSFER_BYTES,
        .dma_enabled = BOARD_REV1_ADC_SPI_DMA_ENABLED,
    };
    fw_status_t status = platform_spi_bus_initialize(
        &bus_config, &s_adc_spi_bus, error);
    if (status != FW_STATUS_OK) {
        release_adc_spi_best_effort();
        return status;
    }

    const platform_spi_device_config_t device_config = {
        .chip_select_pin = BOARD_REV1_GPIO_ADC_CS,
        .chip_select_polarity = PLATFORM_SPI_CS_ACTIVE_LOW,
        .bit_order = BOARD_REV1_ADC_SPI_BIT_ORDER,
        .mode = BOARD_REV1_ADC_SPI_MODE,
        .filler_byte = UINT8_C(0),
        .initial_clock_hz = BOARD_REV1_ADC_SPI_INITIAL_CLOCK_HZ,
        .maximum_clock_hz = BOARD_REV1_ADC_SPI_MAX_READ_CLOCK_HZ,
        .input_delay_ns = 0U,
        .chip_select_setup_us = BOARD_REV1_ADC_SPI_CS_SETUP_US,
        .chip_select_hold_us = BOARD_REV1_ADC_SPI_CS_HOLD_US,
        .maximum_transfer_size_bytes =
            BOARD_REV1_ADC_SPI_MAX_TRANSFER_BYTES,
    };
    status = platform_spi_device_add(s_adc_spi_bus, &device_config,
                                     &s_adc_spi_device, error);
    if (status != FW_STATUS_OK) {
        release_adc_spi_best_effort();
        return status;
    }

    const ad7779_config_t adc_config = {
        .spi = platform_spi_device_interface(s_adc_spi_device),
        .set_mclk_enabled = set_adc_mclk_enabled,
        .set_reset_asserted = set_adc_reset_asserted,
        .set_start_high = set_adc_start_high,
        .delay_us = delay_adc_control,
        .control_context = NULL,
        .delay_context = NULL,
        .spi_timeout_us = BOARD_REV1_ADC_SPI_TRANSFER_TIMEOUT_US,
        .mclk_hz = BOARD_REV1_ADC_MCLK_HZ,
        .mclk_settling_us = BOARD_REV1_ADC_MCLK_SETTLING_US,
        .reset_assert_us = BOARD_REV1_ADC_RESET_ASSERT_US,
        .reset_release_us = BOARD_REV1_ADC_RESET_RELEASE_US,
        .init_timeout_us = BOARD_REV1_ADC_INIT_TIMEOUT_US,
        .init_poll_interval_us = BOARD_REV1_ADC_INIT_POLL_INTERVAL_US,
        .instance = BOARD_REV1_ADC_INSTANCE,
    };
    status = ad7779_initialize(adc, &adc_config, error);
    if (status != FW_STATUS_OK) {
        fw_error_context_t original_error;

        if (error != NULL) {
            original_error = *error;
        }
        (void)ad7779_deinitialize(adc, NULL);
        release_adc_spi_best_effort();
        if (error != NULL) {
            *error = original_error;
        }
        return status;
    }

    s_adc_owner = adc;
    return FW_STATUS_OK;
}

fw_status_t board_adc_get_spi_clock(uint32_t *requested_clock_hz,
                                    uint32_t *actual_clock_hz,
                                    fw_error_context_t *error)
{
    clear_error(error);
    if (s_adc_spi_device == NULL || s_adc_owner == NULL) {
        return set_board_error(error, FW_STATUS_NOT_INITIALIZED,
                               FW_ERROR_OPERATION_GET_CLOCK,
                               BOARD_REV1_ADC_INSTANCE);
    }
    return platform_spi_device_get_clock(s_adc_spi_device,
                                         requested_clock_hz,
                                         actual_clock_hz, error);
}

fw_status_t board_adc_deinitialize(ad7779_t *adc,
                                   fw_error_context_t *error)
{
    clear_error(error);
    if (adc == NULL) {
        return set_board_error(error, FW_STATUS_INVALID_ARGUMENT,
                               FW_ERROR_OPERATION_DEINITIALIZE,
                               BOARD_REV1_ADC_INSTANCE);
    }
    if (s_adc_owner != adc || s_adc_spi_device == NULL ||
        s_adc_spi_bus == NULL) {
        return set_board_error(error, FW_STATUS_NOT_INITIALIZED,
                               FW_ERROR_OPERATION_DEINITIALIZE,
                               BOARD_REV1_ADC_INSTANCE);
    }

    fw_status_t status = ad7779_deinitialize(adc, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = platform_spi_device_remove(
        s_adc_spi_device, BOARD_REV1_ADC_SPI_TRANSFER_TIMEOUT_US, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    s_adc_spi_device = NULL;
    status = platform_spi_bus_deinitialize(
        s_adc_spi_bus, BOARD_REV1_ADC_SPI_TRANSFER_TIMEOUT_US, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    s_adc_spi_bus = NULL;
    s_adc_owner = NULL;
    return FW_STATUS_OK;
}
