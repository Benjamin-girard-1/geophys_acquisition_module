#include "ad7779.h"

#define AD7779_DRIVER_INTERNAL 1
#include "ad7779_reg.h"
#undef AD7779_DRIVER_INTERNAL

#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t address;
    uint8_t expected;
} ad7779_reset_register_t;

_Static_assert(AD7779_RAW_FRAME_BYTES == AD7779_FRAME_BYTES_TOTAL,
               "public and private AD7779 frame sizes must agree");

static const ad7779_reset_register_t s_reset_registers[] = {
    {AD7779_REG_CH_DISABLE, AD7779_CH_DISABLE_RESET_VALUE},
    {AD7779_REG_GENERAL_USER_CONFIG_1, AD7779_GUC1_RESET_VALUE},
    {AD7779_REG_GENERAL_USER_CONFIG_2, AD7779_GUC2_RESET_VALUE},
    {AD7779_REG_GENERAL_USER_CONFIG_3, AD7779_GUC3_RESET_VALUE},
    {AD7779_REG_DOUT_FORMAT, AD7779_DOUT_RESET_VALUE},
    {AD7779_REG_BUFFER_CONFIG_1, AD7779_BC1_RESET_VALUE},
    {AD7779_REG_BUFFER_CONFIG_2, AD7779_BC2_RESET_VALUE},
    {AD7779_REG_GEN_ERR_REG_1_EN, AD7779_ERR1_EN_RESET_VALUE},
    {AD7779_REG_GEN_ERR_REG_2_EN, AD7779_ERR2_EN_RESET_VALUE},
    {AD7779_REG_SRC_N_MSB, AD7779_SRC_N_MSB_RESET_VALUE},
    {AD7779_REG_SRC_N_LSB, AD7779_SRC_N_LSB_RESET_VALUE},
};

static void clear_error(fw_error_context_t *error)
{
    if (error != NULL) {
        error->status = FW_STATUS_OK;
        error->resource = FW_ERROR_RESOURCE_NONE;
        error->operation = FW_ERROR_OPERATION_NONE;
        error->instance = FW_ERROR_INSTANCE_NONE;
        error->detail = 0U;
    }
}

static fw_status_t set_error(fw_error_context_t *error,
                             fw_status_t status,
                             fw_error_operation_t operation,
                             uint32_t instance,
                             uint32_t detail)
{
    if (error != NULL) {
        error->status = status;
        error->resource = FW_ERROR_RESOURCE_ADC;
        error->operation = operation;
        error->instance = instance;
        error->detail = detail;
    }
    return status;
}

static uint32_t device_instance(const ad7779_t *device)
{
    return (device != NULL && device->bound) ? device->config.instance
                                              : FW_ERROR_INSTANCE_NONE;
}

static fw_status_t require_bound(const ad7779_t *device,
                                 fw_error_operation_t operation,
                                 fw_error_context_t *error)
{
    if (device == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT, operation,
                         FW_ERROR_INSTANCE_NONE, 0U);
    }
    if (!device->bound || device->state == AD7779_STATE_UNINITIALIZED) {
        return set_error(error, FW_STATUS_NOT_INITIALIZED, operation,
                         FW_ERROR_INSTANCE_NONE, 0U);
    }
    return FW_STATUS_OK;
}

static bool config_is_valid(const ad7779_config_t *config)
{
    return config != NULL && config->spi.transfer != NULL &&
           config->set_mclk_enabled != NULL &&
           config->set_reset_asserted != NULL &&
           config->set_start_high != NULL && config->delay_us != NULL &&
           config->spi_timeout_us != 0U && config->mclk_hz != 0U &&
           config->mclk_settling_us != 0U &&
           config->reset_assert_us != 0U && config->reset_release_us != 0U &&
           config->init_timeout_us != 0U &&
           config->init_poll_interval_us != 0U;
}

static fw_status_t spi_transfer(ad7779_t *device,
                                const uint8_t *tx_data,
                                uint8_t *rx_data,
                                size_t length_bytes,
                                fw_error_context_t *error)
{
    fw_status_t status = device->config.spi.transfer(
        device->config.spi.context, tx_data, rx_data, length_bytes,
        device->config.spi_timeout_us, error);

    if (status != FW_STATUS_OK && error != NULL && error->status == FW_STATUS_OK) {
        set_error(error, status, FW_ERROR_OPERATION_TRANSFER,
                  device->config.instance, (uint32_t)length_bytes);
    }
    return status;
}

static fw_status_t read_register(ad7779_t *device,
                                 uint8_t address,
                                 uint8_t *value,
                                 fw_error_context_t *error)
{
    uint8_t tx_data[AD7779_SPI_REGISTER_FRAME_BYTES] = {
        (uint8_t)(AD7779_SPI_READ | (address & AD7779_SPI_ADDRESS_MASK)), 0U};
    uint8_t rx_data[AD7779_SPI_REGISTER_FRAME_BYTES] = {0U, 0U};
    fw_status_t status;

    if (value == NULL || address > AD7779_REG_SRC_UPDATE) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, device->config.instance,
                         address);
    }

    status = spi_transfer(device, tx_data, rx_data, sizeof(tx_data), error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (rx_data[0] != AD7779_SPI_REGISTER_HEADER) {
        return set_error(error, FW_STATUS_INTEGRITY, FW_ERROR_OPERATION_READ,
                         device->config.instance,
                         ((uint32_t)address << 8) | rx_data[0]);
    }

    *value = rx_data[1];
    return FW_STATUS_OK;
}

static fw_status_t write_register(ad7779_t *device,
                                  uint8_t address,
                                  uint8_t value,
                                  fw_error_context_t *error)
{
    uint8_t tx_data[AD7779_SPI_REGISTER_FRAME_BYTES] = {
        (uint8_t)(AD7779_SPI_WRITE | (address & AD7779_SPI_ADDRESS_MASK)),
        value};
    uint8_t rx_data[AD7779_SPI_REGISTER_FRAME_BYTES] = {0U, 0U};
    fw_status_t status;

    if (address > AD7779_REG_SRC_UPDATE) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_WRITE, device->config.instance,
                         address);
    }

    status = spi_transfer(device, tx_data, rx_data, sizeof(tx_data), error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (rx_data[0] != AD7779_SPI_REGISTER_HEADER) {
        return set_error(error, FW_STATUS_INTEGRITY, FW_ERROR_OPERATION_WRITE,
                         device->config.instance,
                         ((uint32_t)address << 8) | rx_data[0]);
    }

    if (address == AD7779_REG_GENERAL_USER_CONFIG_3) {
        device->general_user_config_3_shadow = value;
    } else if (address == AD7779_REG_CH_DISABLE) {
        device->channel_disable_shadow = value;
    }
    return FW_STATUS_OK;
}

static fw_status_t set_control(ad7779_t *device,
                               ad7779_set_control_callback_t callback,
                               bool enabled,
                               fw_error_operation_t operation,
                               fw_error_context_t *error)
{
    fw_status_t status = callback(device->config.control_context, enabled, error);

    if (status != FW_STATUS_OK && error != NULL && error->status == FW_STATUS_OK) {
        set_error(error, status, operation, device->config.instance,
                  enabled ? 1U : 0U);
    }
    return status;
}

static void set_fault_state(ad7779_t *device)
{
    device->state = AD7779_STATE_FAULT;
    device->last_status.state = AD7779_STATE_FAULT;
}

static void enter_reset_safe_state_best_effort(ad7779_t *device)
{
    fw_error_context_t ignored_error;

    clear_error(&ignored_error);
    (void)set_control(device, device->config.set_reset_asserted, true,
                      FW_ERROR_OPERATION_ENABLE, &ignored_error);
    clear_error(&ignored_error);
    (void)set_control(device, device->config.set_start_high, true,
                      FW_ERROR_OPERATION_ENABLE, &ignored_error);
    clear_error(&ignored_error);
    (void)set_control(device, device->config.set_mclk_enabled, false,
                      FW_ERROR_OPERATION_DISABLE, &ignored_error);
    set_fault_state(device);
}

static fw_status_t poll_init_complete(ad7779_t *device,
                                      fw_error_context_t *error)
{
    uint32_t elapsed_us = 0U;

    for (;;) {
        uint8_t status_3 = 0U;
        fw_status_t status = read_register(
            device, AD7779_REG_STATUS_REG_3, &status_3, error);
        if (status != FW_STATUS_OK) {
            return status;
        }
        if ((status_3 & AD7779_STAT3_INIT_COMPLETE) != 0U) {
            return FW_STATUS_OK;
        }
        if (elapsed_us >= device->config.init_timeout_us) {
            return set_error(error, FW_STATUS_TIMEOUT, FW_ERROR_OPERATION_WAIT,
                             device->config.instance,
                             device->config.init_timeout_us);
        }

        uint32_t delay_us = device->config.init_poll_interval_us;
        uint32_t remaining_us = device->config.init_timeout_us - elapsed_us;
        if (delay_us > remaining_us) {
            delay_us = remaining_us;
        }
        device->config.delay_us(device->config.delay_context, delay_us);
        elapsed_us += delay_us;
    }
}

static fw_status_t verify_reset_defaults(ad7779_t *device,
                                         fw_error_context_t *error)
{
    size_t index;

    for (index = 0U;
         index < (sizeof(s_reset_registers) / sizeof(s_reset_registers[0]));
         ++index) {
        uint8_t actual = 0U;
        fw_status_t status = read_register(
            device, s_reset_registers[index].address, &actual, error);
        if (status != FW_STATUS_OK) {
            return status;
        }
        if (actual != s_reset_registers[index].expected) {
            uint32_t detail = ((uint32_t)s_reset_registers[index].address << 16) |
                              ((uint32_t)s_reset_registers[index].expected << 8) |
                              actual;
            return set_error(error, FW_STATUS_INTEGRITY,
                             FW_ERROR_OPERATION_READ,
                             device->config.instance, detail);
        }
    }
    return FW_STATUS_OK;
}

static bool encode_channel_gain(ad7779_gain_t gain, uint8_t *encoded)
{
    if (encoded == NULL) {
        return false;
    }

    switch (gain) {
    case AD7779_GAIN_X1:
        *encoded = AD7779_CH_CONFIG_GAIN_1;
        return true;
    case AD7779_GAIN_X2:
        *encoded = AD7779_CH_CONFIG_GAIN_2;
        return true;
    case AD7779_GAIN_X4:
        *encoded = AD7779_CH_CONFIG_GAIN_4;
        return true;
    case AD7779_GAIN_X8:
        *encoded = AD7779_CH_CONFIG_GAIN_8;
        return true;
    default:
        return false;
    }
}

static bool output_rate_is_supported(ad7779_output_rate_t output_rate)
{
    switch (output_rate) {
    case AD7779_OUTPUT_RATE_500_SPS:
    case AD7779_OUTPUT_RATE_1000_SPS:
    case AD7779_OUTPUT_RATE_2000_SPS:
    case AD7779_OUTPUT_RATE_4000_SPS:
    case AD7779_OUTPUT_RATE_8000_SPS:
    case AD7779_OUTPUT_RATE_16000_SPS:
        return true;
    default:
        return false;
    }
}

static bool calculate_src_registers(uint32_t mclk_hz,
                                    ad7779_output_rate_t output_rate,
                                    uint16_t *src_n,
                                    uint16_t *src_if)
{
    const uint64_t maximum_scaled_decimation =
        ((uint64_t)AD7779_SRC_N_MAX * AD7779_SRC_FRACTION_SCALE) +
        AD7779_SRC_IF_MAX;
    const uint64_t minimum_scaled_decimation =
        (uint64_t)AD7779_HR_SRC_N_MIN * AD7779_SRC_FRACTION_SCALE;
    uint64_t denominator;
    uint64_t scaled_decimation;

    if (mclk_hz == 0U || src_n == NULL || src_if == NULL ||
        !output_rate_is_supported(output_rate)) {
        return false;
    }

    denominator = (uint64_t)AD7779_HR_MCLK_DIV * (uint32_t)output_rate;
    scaled_decimation =
        (((uint64_t)mclk_hz * AD7779_SRC_FRACTION_SCALE) +
         (denominator / 2U)) /
        denominator;

    /*
     * With the Rev-1 8.192 MHz MCLK, 500 SPS requests a decimation of
     * exactly 4096. The AD7779 maximum is one fractional LSB lower, which
     * produces approximately 500 SPS. Reject larger out-of-range requests.
     */
    if (scaled_decimation == maximum_scaled_decimation + 1U) {
        scaled_decimation = maximum_scaled_decimation;
    }
    if (scaled_decimation < minimum_scaled_decimation ||
        scaled_decimation > maximum_scaled_decimation) {
        return false;
    }

    *src_n = (uint16_t)(scaled_decimation / AD7779_SRC_FRACTION_SCALE);
    *src_if = (uint16_t)(scaled_decimation % AD7779_SRC_FRACTION_SCALE);
    return true;
}

static fw_status_t verify_register_value(ad7779_t *device,
                                         uint8_t address,
                                         uint8_t expected,
                                         fw_error_context_t *error)
{
    uint8_t actual = 0U;
    fw_status_t status = read_register(device, address, &actual, error);

    if (status != FW_STATUS_OK) {
        return status;
    }
    if (actual != expected) {
        uint32_t detail = ((uint32_t)address << 16) |
                          ((uint32_t)expected << 8) | actual;
        return set_error(error, FW_STATUS_INTEGRITY, FW_ERROR_OPERATION_READ,
                         device->config.instance, detail);
    }
    return FW_STATUS_OK;
}

static fw_status_t fail_channel_configuration(ad7779_t *device,
                                              fw_status_t failure_status,
                                              fw_error_context_t *error)
{
    fw_error_context_t original_error;
    fw_error_context_t ignored_error;

    if (error != NULL) {
        original_error = *error;
    }
    clear_error(&ignored_error);
    (void)write_register(device, AD7779_REG_CH_DISABLE,
                         AD7779_ALL_CHANNELS_MASK, &ignored_error);
    device->channel_configured = false;
    set_fault_state(device);
    if (error != NULL) {
        *error = original_error;
    }
    return failure_status;
}

static fw_status_t fail_output_rate_configuration(
    ad7779_t *device,
    fw_status_t failure_status)
{
    device->output_rate_configured = false;
    set_fault_state(device);
    return failure_status;
}

static ad7779_fault_flags_t normalize_general_faults(uint8_t error_1,
                                                      uint8_t error_2)
{
    ad7779_fault_flags_t faults = AD7779_FAULT_NONE;

    if ((error_1 & AD7779_ERR1_MEMMAP_CRC) != 0U) {
        faults |= AD7779_FAULT_MEMMAP_CRC;
    }
    if ((error_1 & AD7779_ERR1_ROM_CRC) != 0U) {
        faults |= AD7779_FAULT_ROM_CRC;
    }
    if ((error_1 & AD7779_ERR1_SPI_CLK_COUNT) != 0U) {
        faults |= AD7779_FAULT_SPI_CLOCK_COUNT;
    }
    if ((error_1 & AD7779_ERR1_SPI_INVALID_READ) != 0U) {
        faults |= AD7779_FAULT_SPI_INVALID_READ;
    }
    if ((error_1 & AD7779_ERR1_SPI_INVALID_WRITE) != 0U) {
        faults |= AD7779_FAULT_SPI_INVALID_WRITE;
    }
    if ((error_1 & AD7779_ERR1_SPI_CRC) != 0U) {
        faults |= AD7779_FAULT_SPI_CRC;
    }
    if ((error_2 & AD7779_ERR2_EXT_MCLK_SWITCH) != 0U) {
        faults |= AD7779_FAULT_EXTERNAL_MCLK;
    }
    if ((error_2 & AD7779_ERR2_ALDO1_PSM) != 0U) {
        faults |= AD7779_FAULT_ALDO1;
    }
    if ((error_2 & AD7779_ERR2_ALDO2_PSM) != 0U) {
        faults |= AD7779_FAULT_ALDO2;
    }
    if ((error_2 & AD7779_ERR2_DLDO_PSM) != 0U) {
        faults |= AD7779_FAULT_DLDO;
    }
    return faults;
}

static fw_status_t collect_status(ad7779_t *device,
                                  bool expect_reset,
                                  ad7779_status_t *normalized,
                                  fw_error_context_t *error)
{
    uint8_t status_1 = 0U;
    uint8_t status_2 = 0U;
    uint8_t status_3 = 0U;
    uint8_t error_1 = 0U;
    uint8_t error_2 = 0U;
    fw_status_t status;
    ad7779_status_t result = {
        .state = device->state,
        .faults = AD7779_FAULT_NONE,
        .init_complete = false,
        .reset_detected = false,
    };

    status = read_register(device, AD7779_REG_STATUS_REG_1, &status_1, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = read_register(device, AD7779_REG_STATUS_REG_2, &status_2, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = read_register(device, AD7779_REG_STATUS_REG_3, &status_3, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = read_register(device, AD7779_REG_GEN_ERR_REG_1, &error_1, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    status = read_register(device, AD7779_REG_GEN_ERR_REG_2, &error_2, error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    result.init_complete = (status_3 & AD7779_STAT3_INIT_COMPLETE) != 0U;
    result.reset_detected = (error_2 & AD7779_ERR2_RESET_DETECTED) != 0U;
    result.faults = normalize_general_faults(error_1, error_2);

    if ((status_1 & UINT8_C(0x1F)) != 0U ||
        (status_2 & UINT8_C(0x07)) != 0U) {
        result.faults |= AD7779_FAULT_CHANNEL_INPUT;
    }
    if ((status_3 & UINT8_C(0x0F)) != 0U) {
        result.faults |= AD7779_FAULT_CHANNEL_SATURATION;
    }
    if (!expect_reset && result.reset_detected) {
        result.faults |= AD7779_FAULT_UNEXPECTED_RESET;
    }
    if (((status_1 | status_2 | status_3) & AD7779_STAT1_CHIP_ERROR) != 0U &&
        result.faults == AD7779_FAULT_NONE &&
        !(expect_reset && result.reset_detected)) {
        result.faults |= AD7779_FAULT_UNKNOWN;
    }

    device->last_status = result;
    if (normalized != NULL) {
        *normalized = result;
    }

    if (!result.init_complete || (expect_reset && !result.reset_detected)) {
        return set_error(error, FW_STATUS_INTEGRITY, FW_ERROR_OPERATION_READ,
                         device->config.instance,
                         ((uint32_t)status_3 << 16) |
                             ((uint32_t)error_2 << 8) |
                             (expect_reset ? 1U : 0U));
    }
    if (result.faults != AD7779_FAULT_NONE) {
        return set_error(error, FW_STATUS_HARDWARE_FAULT,
                         FW_ERROR_OPERATION_READ, device->config.instance,
                         result.faults);
    }
    return FW_STATUS_OK;
}

static fw_status_t stop_hardware(ad7779_t *device,
                                 bool preserve_fault_state,
                                 fw_error_context_t *error)
{
    fw_status_t first_status = FW_STATUS_OK;
    fw_error_context_t first_error;
    fw_error_context_t current_error;
    uint8_t stopped_config = (uint8_t)(device->general_user_config_3_shadow &
                                       (uint8_t)~AD7779_GUC3_SPI_SUBORDINATE_MODE_EN);

    clear_error(&first_error);
    clear_error(&current_error);

    fw_status_t status = write_register(
        device, AD7779_REG_GENERAL_USER_CONFIG_3, stopped_config,
        &current_error);
    if (status != FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }

    clear_error(&current_error);
    status = write_register(device, AD7779_REG_CH_DISABLE,
                            AD7779_ALL_CHANNELS_MASK, &current_error);
    if (status != FW_STATUS_OK && first_status == FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }

    if (first_status != FW_STATUS_OK) {
        set_fault_state(device);
        if (error != NULL) {
            *error = first_error;
        }
        return first_status;
    }

    if (!preserve_fault_state) {
        device->state = AD7779_STATE_STOPPED;
        device->last_status.state = AD7779_STATE_STOPPED;
    }
    clear_error(error);
    return FW_STATUS_OK;
}

static fw_status_t perform_reset(ad7779_t *device,
                                 fw_error_context_t *error)
{
    fw_status_t status;

    memset(&device->applied_channel_config, 0,
           sizeof(device->applied_channel_config));
    device->channel_configured = false;
    device->applied_output_rate = 0;
    device->output_rate_configured = false;
    device->state = AD7779_STATE_INITIALIZING;
    memset(&device->last_status, 0, sizeof(device->last_status));
    device->last_status.state = AD7779_STATE_INITIALIZING;

    status = set_control(device, device->config.set_start_high, false,
                         FW_ERROR_OPERATION_DISABLE, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    status = set_control(device, device->config.set_mclk_enabled, true,
                         FW_ERROR_OPERATION_ENABLE, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    device->config.delay_us(device->config.delay_context,
                            device->config.mclk_settling_us);

    /* Safe state holds RESET low. Release it before generating a new pulse. */
    status = set_control(device, device->config.set_reset_asserted, false,
                         FW_ERROR_OPERATION_DISABLE, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    device->config.delay_us(device->config.delay_context,
                            device->config.reset_release_us);
    status = set_control(device, device->config.set_reset_asserted, true,
                         FW_ERROR_OPERATION_ENABLE, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    device->config.delay_us(device->config.delay_context,
                            device->config.reset_assert_us);
    status = set_control(device, device->config.set_reset_asserted, false,
                         FW_ERROR_OPERATION_DISABLE, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    device->config.delay_us(device->config.delay_context,
                            device->config.reset_release_us);
    status = set_control(device, device->config.set_start_high, true,
                         FW_ERROR_OPERATION_ENABLE, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }

    status = poll_init_complete(device, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    status = verify_reset_defaults(device, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }

    device->general_user_config_3_shadow = AD7779_GUC3_RESET_VALUE;
    device->channel_disable_shadow = AD7779_CH_DISABLE_RESET_VALUE;
    status = collect_status(device, false, NULL, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    status = stop_hardware(device, false, error);
    if (status != FW_STATUS_OK) {
        goto failure;
    }
    return FW_STATUS_OK;

failure:
    enter_reset_safe_state_best_effort(device);
    return status;
}

fw_status_t ad7779_initialize(ad7779_t *device,
                              const ad7779_config_t *config,
                              fw_error_context_t *error)
{
    clear_error(error);
    if (device == NULL || !config_is_valid(config)) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_INITIALIZE,
                         (config != NULL) ? config->instance
                                          : FW_ERROR_INSTANCE_NONE,
                         0U);
    }
    if (device->bound) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_INITIALIZE,
                         device_instance(device), device->state);
    }

    memset(device, 0, sizeof(*device));
    device->config = *config;
    device->bound = true;
    device->state = AD7779_STATE_UNINITIALIZED;
    device->general_user_config_3_shadow = AD7779_GUC3_RESET_VALUE;
    device->channel_disable_shadow = AD7779_CH_DISABLE_RESET_VALUE;
    return perform_reset(device, error);
}

fw_status_t ad7779_reset(ad7779_t *device,
                         fw_error_context_t *error)
{
    fw_status_t status;

    clear_error(error);
    status = require_bound(device, FW_ERROR_OPERATION_INITIALIZE, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (device->state == AD7779_STATE_RUNNING ||
        device->state == AD7779_STATE_INITIALIZING) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_INITIALIZE,
                         device->config.instance, device->state);
    }
    return perform_reset(device, error);
}

fw_status_t ad7779_verify_status(ad7779_t *device,
                                 ad7779_status_t *status,
                                 fw_error_context_t *error)
{
    fw_status_t result;

    clear_error(error);
    result = require_bound(device, FW_ERROR_OPERATION_READ, error);
    if (result != FW_STATUS_OK) {
        return result;
    }
    if (status == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, device->config.instance, 0U);
    }
    if (device->state == AD7779_STATE_INITIALIZING ||
        device->state == AD7779_STATE_RUNNING) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_READ, device->config.instance,
                         device->state);
    }

    result = collect_status(device, false, status, error);
    if (result != FW_STATUS_OK) {
        if (!status->init_complete ||
            (status->faults & AD7779_FAULT_UNEXPECTED_RESET) != 0U) {
            device->channel_configured = false;
            device->output_rate_configured = false;
        }
        set_fault_state(device);
        status->state = AD7779_STATE_FAULT;
    }
    return result;
}

fw_status_t ad7779_configure_default_channels(ad7779_t *device,
                                              fw_error_context_t *error)
{
    ad7779_channel_config_t configuration = {
        .enabled_mask = AD7779_CHANNEL_MASK_ALL,
        .gains = {
            AD7779_GAIN_X1,
            AD7779_GAIN_X1,
            AD7779_GAIN_X1,
            AD7779_GAIN_X1,
            AD7779_GAIN_X1,
            AD7779_GAIN_X1,
            AD7779_GAIN_X1,
            AD7779_GAIN_X1,
        },
    };

    return ad7779_configure_channels(device, &configuration, error);
}

fw_status_t ad7779_configure_channels(
    ad7779_t *device,
    const ad7779_channel_config_t *configuration,
    fw_error_context_t *error)
{
    uint8_t encoded_gains[AD7779_CHANNEL_COUNT];
    fw_status_t status;
    size_t channel;

    clear_error(error);
    status = require_bound(device, FW_ERROR_OPERATION_CONFIGURE, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (configuration == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_CONFIGURE,
                         device->config.instance, 0U);
    }
    if (device->state != AD7779_STATE_STOPPED) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_CONFIGURE,
                         device->config.instance, device->state);
    }

    for (channel = 0U; channel < AD7779_CHANNEL_COUNT; ++channel) {
        if (!encode_channel_gain(configuration->gains[channel],
                                 &encoded_gains[channel])) {
            return set_error(
                error, FW_STATUS_INVALID_ARGUMENT,
                FW_ERROR_OPERATION_CONFIGURE, device->config.instance,
                ((uint32_t)channel << 16) |
                    (uint32_t)configuration->gains[channel]);
        }
    }

    status = write_register(device, AD7779_REG_CH_DISABLE,
                            AD7779_ALL_CHANNELS_MASK, error);
    if (status != FW_STATUS_OK) {
        return fail_channel_configuration(device, status, error);
    }
    status = verify_register_value(device, AD7779_REG_CH_DISABLE,
                                   AD7779_ALL_CHANNELS_MASK, error);
    if (status != FW_STATUS_OK) {
        return fail_channel_configuration(device, status, error);
    }

    for (channel = 0U; channel < AD7779_CHANNEL_COUNT; ++channel) {
        uint8_t address = AD7779_REG_CH_CONFIG((uint8_t)channel);

        status = write_register(device, address, encoded_gains[channel], error);
        if (status != FW_STATUS_OK) {
            return fail_channel_configuration(device, status, error);
        }
        status = verify_register_value(device, address,
                                       encoded_gains[channel], error);
        if (status != FW_STATUS_OK) {
            return fail_channel_configuration(device, status, error);
        }
    }

    device->applied_channel_config = *configuration;
    device->channel_configured = true;
    return FW_STATUS_OK;
}

fw_status_t ad7779_get_channel_configuration(
    const ad7779_t *device,
    ad7779_channel_config_t *configuration,
    fw_error_context_t *error)
{
    fw_status_t status;

    clear_error(error);
    status = require_bound(device, FW_ERROR_OPERATION_READ, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (configuration == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ,
                         device->config.instance, 0U);
    }
    if (!device->channel_configured) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_READ,
                         device->config.instance, device->state);
    }

    *configuration = device->applied_channel_config;
    return FW_STATUS_OK;
}

fw_status_t ad7779_configure_output_rate(
    ad7779_t *device,
    ad7779_output_rate_t output_rate,
    fw_error_context_t *error)
{
    uint16_t src_n = 0U;
    uint16_t src_if = 0U;
    uint8_t general_user_config_1 = 0U;
    uint32_t update_hold_us;
    fw_status_t status;

    clear_error(error);
    status = require_bound(device, FW_ERROR_OPERATION_CONFIGURE, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (!output_rate_is_supported(output_rate)) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_CONFIGURE,
                         device->config.instance, (uint32_t)output_rate);
    }
    if (device->state != AD7779_STATE_STOPPED) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_CONFIGURE,
                         device->config.instance, device->state);
    }
    if (!calculate_src_registers(device->config.mclk_hz, output_rate,
                                 &src_n, &src_if)) {
        return set_error(error, FW_STATUS_UNSUPPORTED,
                         FW_ERROR_OPERATION_CONFIGURE,
                         device->config.instance, (uint32_t)output_rate);
    }

    status = read_register(device, AD7779_REG_GENERAL_USER_CONFIG_1,
                           &general_user_config_1, error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    general_user_config_1 |= AD7779_GUC1_HR_MODE;
    status = write_register(device, AD7779_REG_GENERAL_USER_CONFIG_1,
                            general_user_config_1, error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = verify_register_value(device, AD7779_REG_GENERAL_USER_CONFIG_1,
                                   general_user_config_1, error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }

    status = write_register(device, AD7779_REG_SRC_N_MSB,
                            (uint8_t)((src_n >> 8) & AD7779_SRC_N_MSB_MSK),
                            error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = verify_register_value(
        device, AD7779_REG_SRC_N_MSB,
        (uint8_t)((src_n >> 8) & AD7779_SRC_N_MSB_MSK), error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = write_register(device, AD7779_REG_SRC_N_LSB,
                            (uint8_t)(src_n & UINT16_C(0x00FF)), error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = verify_register_value(device, AD7779_REG_SRC_N_LSB,
                                   (uint8_t)(src_n & UINT16_C(0x00FF)), error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = write_register(device, AD7779_REG_SRC_IF_MSB,
                            (uint8_t)(src_if >> 8), error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = verify_register_value(device, AD7779_REG_SRC_IF_MSB,
                                   (uint8_t)(src_if >> 8), error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = write_register(device, AD7779_REG_SRC_IF_LSB,
                            (uint8_t)(src_if & UINT16_C(0x00FF)), error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = verify_register_value(
        device, AD7779_REG_SRC_IF_LSB,
        (uint8_t)(src_if & UINT16_C(0x00FF)), error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }

    status = write_register(device, AD7779_REG_SRC_UPDATE,
                            AD7779_SRC_LOAD_UPDATE, error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = verify_register_value(device, AD7779_REG_SRC_UPDATE,
                                   AD7779_SRC_LOAD_UPDATE, error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }

    update_hold_us =
        (uint32_t)((((uint64_t)AD7779_SRC_UPDATE_MIN_MCLK_CYCLES *
                     UINT64_C(1000000)) +
                    device->config.mclk_hz - 1U) /
                   device->config.mclk_hz);
    device->config.delay_us(device->config.delay_context, update_hold_us);

    status = write_register(device, AD7779_REG_SRC_UPDATE, 0U, error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }
    status = verify_register_value(device, AD7779_REG_SRC_UPDATE, 0U, error);
    if (status != FW_STATUS_OK) {
        return fail_output_rate_configuration(device, status);
    }

    device->applied_output_rate = output_rate;
    device->output_rate_configured = true;
    return FW_STATUS_OK;
}

fw_status_t ad7779_get_output_rate(
    const ad7779_t *device,
    ad7779_output_rate_t *output_rate,
    fw_error_context_t *error)
{
    fw_status_t status;

    clear_error(error);
    status = require_bound(device, FW_ERROR_OPERATION_READ, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (output_rate == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ,
                         device->config.instance, 0U);
    }
    if (!device->output_rate_configured) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_READ,
                         device->config.instance, device->state);
    }

    *output_rate = device->applied_output_rate;
    return FW_STATUS_OK;
}

fw_status_t ad7779_decode_frame(
    const uint8_t *raw_frame,
    size_t raw_frame_size,
    int32_t samples[AD7779_CHANNEL_COUNT],
    fw_error_context_t *error)
{
    int32_t decoded_samples[AD7779_CHANNEL_COUNT];
    size_t channel;

    clear_error(error);
    if (raw_frame == NULL || samples == NULL ||
        raw_frame_size != AD7779_RAW_FRAME_BYTES) {
        uint32_t detail = raw_frame_size > UINT32_MAX
                              ? UINT32_MAX
                              : (uint32_t)raw_frame_size;
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, FW_ERROR_INSTANCE_NONE,
                         detail);
    }

    for (channel = 0U; channel < AD7779_CHANNEL_COUNT; ++channel) {
        size_t sample_offset =
            (channel * AD7779_RAW_BYTES_PER_CHANNEL) +
            AD7779_FRAME_HEADER_BYTES;
        uint32_t raw_sample =
            ((uint32_t)raw_frame[sample_offset] << 16) |
            ((uint32_t)raw_frame[sample_offset + 1U] << 8) |
            (uint32_t)raw_frame[sample_offset + 2U];
        int32_t signed_sample = (int32_t)raw_sample;

        if ((raw_sample & AD7779_SAMPLE_SIGN_BIT) != 0U) {
            signed_sample -= (int32_t)AD7779_SAMPLE_MODULUS;
        }
        decoded_samples[channel] = signed_sample;
    }

    memcpy(samples, decoded_samples, sizeof(decoded_samples));
    return FW_STATUS_OK;
}

static uint8_t crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = AD7779_CRC_INITIAL_VALUE;
    size_t byte_index;

    for (byte_index = 0U; byte_index < length; ++byte_index) {
        uint8_t bit;

        crc ^= data[byte_index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc & UINT8_C(0x80)) != 0U
                      ? (uint8_t)((uint8_t)(crc << 1) ^
                                  AD7779_CRC_POLYNOMIAL)
                      : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint8_t frame_pair_crc(const uint8_t *even_channel_frame,
                              const uint8_t *odd_channel_frame)
{
    uint8_t input[AD7779_FRAME_CRC_INPUT_BYTES_PER_PAIR];

    /* The CRC covers header bits [7:4] and 24 data bits for each channel. */
    input[0] = (uint8_t)((even_channel_frame[0] & UINT8_C(0xF0)) |
                         (even_channel_frame[1] >> 4));
    input[1] = (uint8_t)((uint8_t)(even_channel_frame[1] << 4) |
                         (even_channel_frame[2] >> 4));
    input[2] = (uint8_t)((uint8_t)(even_channel_frame[2] << 4) |
                         (even_channel_frame[3] >> 4));
    input[3] = (uint8_t)((uint8_t)(even_channel_frame[3] << 4) |
                         (odd_channel_frame[0] >> 4));
    input[4] = odd_channel_frame[1];
    input[5] = odd_channel_frame[2];
    input[6] = odd_channel_frame[3];

    return crc8(input, sizeof(input));
}

static void validate_status_header(uint8_t header,
                                   size_t channel,
                                   ad7779_frame_validation_t *validation)
{
    uint8_t status = header & AD7779_FRAME_HEADER_LOW_NIBBLE_MSK;
    uint8_t channel_bit = (uint8_t)(UINT8_C(1) << channel);

    if ((status & AD7779_FRAME_STATUS_RESET_DETECTED) != 0U) {
        validation->faults |= AD7779_FAULT_UNEXPECTED_RESET;
        validation->affected_channel_mask |= AD7779_CHANNEL_MASK_ALL;
    }
    if ((status & (AD7779_FRAME_STATUS_MODULATOR_SAT |
                   AD7779_FRAME_STATUS_FILTER_SAT)) != 0U) {
        validation->faults |= AD7779_FAULT_CHANNEL_SATURATION;
        validation->affected_channel_mask |= channel_bit;
    }
    if ((status & AD7779_FRAME_STATUS_AIN_OV_UV) != 0U) {
        validation->faults |= AD7779_FAULT_CHANNEL_INPUT;
        validation->affected_channel_mask |= channel_bit;
    }
}

fw_status_t ad7779_validate_frame(
    const uint8_t *raw_frame,
    size_t raw_frame_size,
    ad7779_frame_header_mode_t header_mode,
    ad7779_frame_validation_t *validation,
    fw_error_context_t *error)
{
    ad7779_frame_validation_t result = {0U, 0U, false};
    ad7779_fault_flags_t integrity_faults = AD7779_FAULT_NONE;
    size_t channel;

    clear_error(error);
    if (raw_frame == NULL || validation == NULL ||
        raw_frame_size != AD7779_RAW_FRAME_BYTES ||
        (header_mode != AD7779_FRAME_HEADER_STATUS &&
         header_mode != AD7779_FRAME_HEADER_CRC)) {
        uint32_t detail = raw_frame_size > UINT32_MAX
                              ? UINT32_MAX
                              : (uint32_t)raw_frame_size;
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, FW_ERROR_INSTANCE_NONE,
                         detail);
    }

    for (channel = 0U; channel < AD7779_CHANNEL_COUNT; ++channel) {
        size_t offset = channel * AD7779_RAW_BYTES_PER_CHANNEL;
        uint8_t header = raw_frame[offset];
        uint8_t reported_channel =
            (uint8_t)((header & AD7779_FRAME_HEADER_CHANNEL_MSK) >>
                      AD7779_FRAME_HEADER_CHANNEL_POS);

        if (reported_channel != (uint8_t)channel) {
            uint8_t channel_bit = (uint8_t)(UINT8_C(1) << channel);

            result.faults |= AD7779_FAULT_CHANNEL_ID;
            integrity_faults |= AD7779_FAULT_CHANNEL_ID;
            result.affected_channel_mask |= channel_bit;
        }
        if ((header & AD7779_FRAME_HEADER_ALERT) != 0U) {
            result.device_alert = true;
        }
        if (header_mode == AD7779_FRAME_HEADER_STATUS) {
            validate_status_header(header, channel, &result);
        }
    }

    if (header_mode == AD7779_FRAME_HEADER_CRC) {
        size_t pair;

        for (pair = 0U; pair < AD7779_FRAME_CHANNEL_PAIR_COUNT; ++pair) {
            size_t even_channel = pair * 2U;
            size_t even_offset =
                even_channel * AD7779_RAW_BYTES_PER_CHANNEL;
            size_t odd_offset =
                (even_channel + 1U) * AD7779_RAW_BYTES_PER_CHANNEL;
            uint8_t expected_crc = frame_pair_crc(&raw_frame[even_offset],
                                                   &raw_frame[odd_offset]);
            uint8_t reported_crc = (uint8_t)(
                (uint8_t)((raw_frame[even_offset] &
                           AD7779_FRAME_HEADER_LOW_NIBBLE_MSK)
                          << 4) |
                (raw_frame[odd_offset] &
                 AD7779_FRAME_HEADER_LOW_NIBBLE_MSK));

            if (reported_crc != expected_crc) {
                uint8_t pair_mask =
                    (uint8_t)(UINT8_C(0x03) << even_channel);

                result.faults |= AD7779_FAULT_DATA_CRC;
                integrity_faults |= AD7779_FAULT_DATA_CRC;
                result.affected_channel_mask |= pair_mask;
            }
        }
    }

    if (result.device_alert && result.faults == AD7779_FAULT_NONE) {
        result.faults |= AD7779_FAULT_UNKNOWN;
        result.affected_channel_mask |= AD7779_CHANNEL_MASK_ALL;
    }
    *validation = result;

    if (integrity_faults != AD7779_FAULT_NONE) {
        return set_error(error, FW_STATUS_INTEGRITY,
                         FW_ERROR_OPERATION_READ, FW_ERROR_INSTANCE_NONE,
                         integrity_faults);
    }
    if (result.device_alert || result.faults != AD7779_FAULT_NONE) {
        return set_error(error, FW_STATUS_HARDWARE_FAULT,
                         FW_ERROR_OPERATION_READ, FW_ERROR_INSTANCE_NONE,
                         result.faults);
    }
    return FW_STATUS_OK;
}

fw_status_t ad7779_stop(ad7779_t *device,
                        fw_error_context_t *error)
{
    fw_status_t status;

    clear_error(error);
    status = require_bound(device, FW_ERROR_OPERATION_DISABLE, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (device->state == AD7779_STATE_STOPPED) {
        return FW_STATUS_OK;
    }
    if (device->state == AD7779_STATE_INITIALIZING) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_DISABLE, device->config.instance,
                         device->state);
    }
    return stop_hardware(device, device->state == AD7779_STATE_FAULT, error);
}

fw_status_t ad7779_deinitialize(ad7779_t *device,
                                fw_error_context_t *error)
{
    fw_status_t first_status = FW_STATUS_OK;
    fw_error_context_t first_error;
    fw_error_context_t current_error;
    fw_status_t status;

    clear_error(error);
    if (device == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_DEINITIALIZE,
                         FW_ERROR_INSTANCE_NONE, 0U);
    }
    if (!device->bound || device->state == AD7779_STATE_UNINITIALIZED) {
        memset(device, 0, sizeof(*device));
        return FW_STATUS_OK;
    }
    if (device->state == AD7779_STATE_INITIALIZING) {
        return set_error(error, FW_STATUS_INVALID_STATE,
                         FW_ERROR_OPERATION_DEINITIALIZE,
                         device->config.instance, device->state);
    }

    clear_error(&first_error);
    clear_error(&current_error);
    if (device->state != AD7779_STATE_STOPPED) {
        status = stop_hardware(device, true, &current_error);
        if (status != FW_STATUS_OK) {
            first_status = status;
            first_error = current_error;
        }
    }

    clear_error(&current_error);
    status = set_control(device, device->config.set_reset_asserted, true,
                         FW_ERROR_OPERATION_ENABLE, &current_error);
    if (status != FW_STATUS_OK && first_status == FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }

    clear_error(&current_error);
    status = set_control(device, device->config.set_start_high, true,
                         FW_ERROR_OPERATION_ENABLE, &current_error);
    if (status != FW_STATUS_OK && first_status == FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }

    clear_error(&current_error);
    status = set_control(device, device->config.set_mclk_enabled, false,
                         FW_ERROR_OPERATION_DISABLE, &current_error);
    if (status != FW_STATUS_OK && first_status == FW_STATUS_OK) {
        first_status = status;
        first_error = current_error;
    }

    if (first_status != FW_STATUS_OK) {
        set_fault_state(device);
        if (error != NULL) {
            *error = first_error;
        }
        return first_status;
    }

    memset(device, 0, sizeof(*device));
    return FW_STATUS_OK;
}

fw_status_t ad7779_get_status(const ad7779_t *device,
                              ad7779_status_t *status,
                              fw_error_context_t *error)
{
    fw_status_t result;

    clear_error(error);
    result = require_bound(device, FW_ERROR_OPERATION_READ, error);
    if (result != FW_STATUS_OK) {
        return result;
    }
    if (status == NULL) {
        return set_error(error, FW_STATUS_INVALID_ARGUMENT,
                         FW_ERROR_OPERATION_READ, device->config.instance, 0U);
    }

    *status = device->last_status;
    status->state = device->state;
    return FW_STATUS_OK;
}
