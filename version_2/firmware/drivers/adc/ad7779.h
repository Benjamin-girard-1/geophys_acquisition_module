#ifndef GEOPHYS_AD7779_H
#define GEOPHYS_AD7779_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_error.h"
#include "fw_spi.h"

#define AD7779_CHANNEL_COUNT 8U
#define AD7779_CHANNEL_MASK_ALL UINT8_C(0xFF)
#define AD7779_RAW_BYTES_PER_CHANNEL 4U
#define AD7779_RAW_FRAME_BYTES \
    (AD7779_CHANNEL_COUNT * AD7779_RAW_BYTES_PER_CHANNEL)

/** @brief Driver lifecycle state. STOPPED means initialized and ready. */
typedef enum {
    AD7779_STATE_UNINITIALIZED = 0,
    AD7779_STATE_INITIALIZING,
    AD7779_STATE_STOPPED,
    AD7779_STATE_RUNNING,
    AD7779_STATE_FAULT,
} ad7779_state_t;

/** @brief Normalized device faults; these are not raw register values. */
typedef enum {
    AD7779_FAULT_NONE = 0,
    AD7779_FAULT_MEMMAP_CRC = (1U << 0),
    AD7779_FAULT_ROM_CRC = (1U << 1),
    AD7779_FAULT_SPI_CLOCK_COUNT = (1U << 2),
    AD7779_FAULT_SPI_INVALID_READ = (1U << 3),
    AD7779_FAULT_SPI_INVALID_WRITE = (1U << 4),
    AD7779_FAULT_SPI_CRC = (1U << 5),
    AD7779_FAULT_EXTERNAL_MCLK = (1U << 6),
    AD7779_FAULT_ALDO1 = (1U << 7),
    AD7779_FAULT_ALDO2 = (1U << 8),
    AD7779_FAULT_DLDO = (1U << 9),
    AD7779_FAULT_CHANNEL_INPUT = (1U << 10),
    AD7779_FAULT_CHANNEL_SATURATION = (1U << 11),
    AD7779_FAULT_UNEXPECTED_RESET = (1U << 12),
    AD7779_FAULT_UNKNOWN = (1U << 13),
    AD7779_FAULT_DATA_CRC = (1U << 14),
    AD7779_FAULT_CHANNEL_ID = (1U << 15),
} ad7779_fault_t;

typedef uint32_t ad7779_fault_flags_t;

/** @brief Supported analog front-end gain for one ADC channel. */
typedef enum {
    AD7779_GAIN_X1 = 1,
    AD7779_GAIN_X2 = 2,
    AD7779_GAIN_X4 = 4,
    AD7779_GAIN_X8 = 8,
} ad7779_gain_t;

/** @brief Supported high-resolution output-rate presets, in samples/second. */
typedef enum {
    AD7779_OUTPUT_RATE_500_SPS = 500,
    AD7779_OUTPUT_RATE_1000_SPS = 1000,
    AD7779_OUTPUT_RATE_2000_SPS = 2000,
    AD7779_OUTPUT_RATE_4000_SPS = 4000,
    AD7779_OUTPUT_RATE_8000_SPS = 8000,
    AD7779_OUTPUT_RATE_16000_SPS = 16000,
} ad7779_output_rate_t;

#define AD7779_DEFAULT_OUTPUT_RATE AD7779_OUTPUT_RATE_1000_SPS

/** @brief Intended active-channel mask and independent gain for each channel. */
typedef struct {
    uint8_t enabled_mask;
    ad7779_gain_t gains[AD7779_CHANNEL_COUNT];
} ad7779_channel_config_t;

/** @brief Set or clear one logical AD7779 control through the board module. */
typedef fw_status_t (*ad7779_set_control_callback_t)(
    void *context,
    bool enabled,
    fw_error_context_t *error);

/** @brief Task-context delay callback. */
typedef void (*ad7779_delay_us_callback_t)(void *context,
                                           uint32_t duration_us);

/**
 * @brief Portable configuration copied into driver-owned state.
 *
 * The board callbacks express logical behavior: set_mclk_enabled(true)
 * enables MCLK; set_reset_asserted(true) asserts RESET; set_start_high(true)
 * drives START High. The board remains responsible for physical polarities and
 * shift-register positions.
 */
typedef struct {
    fw_spi_interface_t spi;
    ad7779_set_control_callback_t set_mclk_enabled;
    ad7779_set_control_callback_t set_reset_asserted;
    ad7779_set_control_callback_t set_start_high;
    ad7779_delay_us_callback_t delay_us;
    void *control_context;
    void *delay_context;
    uint32_t spi_timeout_us;
    uint32_t mclk_hz;
    uint32_t mclk_settling_us;
    uint32_t reset_assert_us;
    uint32_t reset_release_us;
    uint32_t init_timeout_us;
    uint32_t init_poll_interval_us;
    uint32_t instance;
} ad7779_config_t;

/** @brief Last verified device status in normalized form. */
typedef struct {
    ad7779_state_t state;
    ad7779_fault_flags_t faults;
    bool init_complete;
    bool reset_detected;
} ad7779_status_t;

/** @brief Meaning of the low nibble in each conversion-frame header. */
typedef enum {
    AD7779_FRAME_HEADER_STATUS = 0,
    AD7779_FRAME_HEADER_CRC,
} ad7779_frame_header_mode_t;

/** @brief Normalized result of validating one simultaneous ADC frame. */
typedef struct {
    ad7779_fault_flags_t faults;
    uint8_t affected_channel_mask;
    bool device_alert;
} ad7779_frame_validation_t;

/**
 * @brief Caller-owned AD7779 state.
 *
 * Zero-initialize before the first call. Do not modify members directly. The
 * owner serializes access; the driver creates no task, lock, or allocation.
 */
typedef struct {
    ad7779_config_t config;
    ad7779_status_t last_status;
    ad7779_channel_config_t applied_channel_config;
    ad7779_output_rate_t applied_output_rate;
    ad7779_state_t state;
    uint8_t general_user_config_3_shadow;
    uint8_t channel_disable_shadow;
    bool channel_configured;
    bool output_rate_configured;
    bool bound;
} ad7779_t;

/**
 * @brief Bind callbacks, reset the device, verify it, and leave it stopped.
 *
 * Required supplies must already be stable. On success all eight channel
 * clocks are disabled and the device is ready for later configuration.
 */
fw_status_t ad7779_initialize(ad7779_t *device,
                              const ad7779_config_t *config,
                              fw_error_context_t *error);

/** @brief Repeat hardware reset and verification from a non-running state. */
fw_status_t ad7779_reset(ad7779_t *device,
                         fw_error_context_t *error);

/** @brief Read and normalize current general device status. */
fw_status_t ad7779_verify_status(ad7779_t *device,
                                 ad7779_status_t *status,
                                 fw_error_context_t *error);

/** @brief Configure all eight selected by default, each at gain x1. */
fw_status_t ad7779_configure_default_channels(ad7779_t *device,
                                              fw_error_context_t *error);

/**
 * @brief Configure the intended channel mask and eight independent gains.
 *
 * The device must be stopped. Channel clocks stay disabled until a later start
 * operation applies enabled_mask.
 */
fw_status_t ad7779_configure_channels(
    ad7779_t *device,
    const ad7779_channel_config_t *configuration,
    fw_error_context_t *error);

/** @brief Return the last completely applied channel configuration. */
fw_status_t ad7779_get_channel_configuration(
    const ad7779_t *device,
    ad7779_channel_config_t *configuration,
    fw_error_context_t *error);

/**
 * @brief Configure one supported output-rate preset in high-resolution mode.
 *
 * The device must be stopped. The configured MCLK frequency is used to encode
 * the SRC registers, and the new setting is cached only after register
 * readback and the software SRC update complete successfully.
 */
fw_status_t ad7779_configure_output_rate(
    ad7779_t *device,
    ad7779_output_rate_t output_rate,
    fw_error_context_t *error);

/** @brief Return the last completely applied output-rate preset. */
fw_status_t ad7779_get_output_rate(
    const ad7779_t *device,
    ad7779_output_rate_t *output_rate,
    fw_error_context_t *error);

/**
 * @brief Decode one simultaneous eight-channel conversion frame.
 *
 * raw_frame must contain exactly 32 bytes in AD7779 SPI output order: one
 * header byte followed by one MSB-first signed 24-bit sample for each channel.
 * This operation sign-extends the samples only. Header, status, and CRC
 * validation are separate operations.
 */
fw_status_t ad7779_decode_frame(
    const uint8_t *raw_frame,
    size_t raw_frame_size,
    int32_t samples[AD7779_CHANNEL_COUNT],
    fw_error_context_t *error);

/**
 * @brief Validate channel IDs and the selected header information.
 *
 * Channel IDs are always checked. Status mode normalizes reset, saturation,
 * analog-input, and ALERT indications. CRC mode checks the four channel-pair
 * CRC values and preserves ALERT as a normalized hardware fault. For a
 * correctly sized frame, validation is populated even when the return status
 * reports INTEGRITY or HARDWARE_FAULT.
 */
fw_status_t ad7779_validate_frame(
    const uint8_t *raw_frame,
    size_t raw_frame_size,
    ad7779_frame_header_mode_t header_mode,
    ad7779_frame_validation_t *validation,
    fw_error_context_t *error);

/**
 * @brief Disable conversion readback and all channel clocks.
 *
 * Calling this operation repeatedly after initialization returns success.
 * MCLK and supplies remain enabled so configuration can be applied later.
 */
fw_status_t ad7779_stop(ad7779_t *device,
                        fw_error_context_t *error);

/** @brief Stop, assert reset, disable MCLK, and clear the bound instance. */
fw_status_t ad7779_deinitialize(ad7779_t *device,
                                fw_error_context_t *error);

/** @brief Return the last cached lifecycle and verification status. */
fw_status_t ad7779_get_status(const ad7779_t *device,
                              ad7779_status_t *status,
                              fw_error_context_t *error);

#endif /* GEOPHYS_AD7779_H */
