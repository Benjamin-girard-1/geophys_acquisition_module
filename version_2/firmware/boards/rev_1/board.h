#ifndef GEOPHYS_BOARD_REV_1_H
#define GEOPHYS_BOARD_REV_1_H

#include <stdbool.h>
#include <stdint.h>

#include "ad7779.h"
#include "fw_error.h"
#include "platform_storage.h"
#include "platform_uart.h"

#define BOARD_HARDWARE_VERSION UINT16_C(2)
#define BOARD_HARDWARE_REVISION UINT16_C(1)

typedef enum {
    BOARD_POWER_RAIL_3V3A = 0,
    BOARD_POWER_RAIL_10V,
    BOARD_POWER_RAIL_NEGATIVE_5V,
    BOARD_POWER_RAIL_18V,
} board_power_rail_t;

typedef struct {
    bool rail_3v3a_enabled;
    bool rail_10v_enabled;
    bool rail_negative_5v_enabled;
    bool rail_18v_enabled;
} board_power_rail_state_t;

typedef struct {
    bool rail_3v3a_enabled;
    bool rail_10v_enabled;
    bool rail_negative_5v_enabled;
} board_acquisition_power_state_t;

typedef enum {
    BOARD_CARD_SLOT_INVALID = 0,
    BOARD_CARD_SLOT_1,
    BOARD_CARD_SLOT_2,
} board_card_slot_t;

typedef enum {
    BOARD_CARD_TYPE_UNKNOWN = 0,
    BOARD_CARD_TYPE_ABSENT,
    BOARD_CARD_TYPE_MAGNETIC,
    BOARD_CARD_TYPE_ACC_GEOPH,
    BOARD_CARD_TYPE_RESISTIVITY,
} board_card_type_t;

typedef enum {
    BOARD_MAGNETIC_PULSE_INVALID = 0,
    BOARD_MAGNETIC_PULSE_SET,
    BOARD_MAGNETIC_PULSE_RESET,
} board_magnetic_pulse_t;

typedef void (*board_adc_drdy_handler_t)(void *context);

/** @brief Calibrated and aggregated analog-ID voltage for one card slot. */
typedef struct {
    uint32_t average_mv;
    uint32_t median_mv;
    uint32_t minimum_mv;
    uint32_t maximum_mv;
    uint16_t sample_count;
} board_card_id_measurement_t;

/**
 * @brief Establish direct safe GPIO states and apply the complete safe image.
 *
 * The shift-register outputs remain disabled until all 16 safe bits have been
 * latched. GPIO42 is deliberately left high impedance.
 */
fw_status_t board_init(fw_error_context_t *error);

/** Read the active-low BOOT pushbutton on the ESP32-S3 DevKitC. */
fw_status_t board_boot_button_pressed(bool *pressed,
                                      fw_error_context_t *error);

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

/** @brief Return the latched state of every software-controlled rail. */
fw_status_t board_get_power_rail_state(
    board_power_rail_state_t *state,
    fw_error_context_t *error);

/**
 * @brief Apply independently requested acquisition-rail states safely.
 *
 * Rails that are being disabled are changed in reverse startup order. Rails
 * that are being enabled are changed in startup order with the documented
 * per-rail settling delay. The pulse-only 18 V rail is not affected.
 */
fw_status_t board_set_acquisition_power_state(
    const board_acquisition_power_state_t *state,
    fw_error_context_t *error);

/**
 * @brief Measure one Rev-1 card-slot analog ID over the configured window.
 *
 * This startup/stopped-acquisition operation returns calibrated millivolts
 * without exposing ESP32 ADC details. It does not classify the card type.
 */
fw_status_t board_measure_card_id(
    board_card_slot_t slot,
    board_card_id_measurement_t *measurement,
    fw_error_context_t *error);

/** @brief Measure and classify one card using the Rev-1 ID network. */
fw_status_t board_detect_card(
    board_card_slot_t slot,
    board_card_type_t *type,
    board_card_id_measurement_t *measurement,
    fw_error_context_t *error);

/** @brief Force all four magnetic SET/RESET outputs inactive atomically. */
fw_status_t board_magnetic_pulse_outputs_safe(fw_error_context_t *error);

/**
 * @brief Drive one logical magnetic pulse output with mutual exclusion.
 *
 * Enabling one output atomically clears every other SET/RESET output first.
 * Disabling an output returns all pulse outputs to their safe state.
 */
fw_status_t board_set_magnetic_pulse_output(
    board_card_slot_t slot,
    board_magnetic_pulse_t pulse,
    bool enabled,
    fw_error_context_t *error);

/**
 * @brief Bind and initialize the Rev-1 host UART without exposing its pins.
 *
 * The caller owns the returned UART instance and its runtime byte movement.
 */
fw_status_t board_host_uart_initialize(platform_uart_t **uart,
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

/** Force the pulse path safe, then power and settle the acquisition rails. */
fw_status_t board_adc_power_up(fw_error_context_t *error);

/** Disable the acquisition rails after ADC deinitialization. */
fw_status_t board_adc_power_down(fw_error_context_t *error);

/** @brief Return the requested and achieved Rev-1 AD7779 SPI clock. */
fw_status_t board_adc_get_spi_clock(uint32_t *requested_clock_hz,
                                    uint32_t *actual_clock_hz,
                                    fw_error_context_t *error);

/** @brief Stop the ADC and release its Rev-1 SPI resources. */
fw_status_t board_adc_deinitialize(ad7779_t *adc,
                                   fw_error_context_t *error);

/** Attach the Rev-1 AD7779 DRDY falling-edge ISR, initially disabled. */
fw_status_t board_adc_drdy_attach(board_adc_drdy_handler_t handler,
                                  void *context,
                                  fw_error_context_t *error);
fw_status_t board_adc_drdy_enable(fw_error_context_t *error);
fw_status_t board_adc_drdy_disable(fw_error_context_t *error);
fw_status_t board_adc_drdy_detach(fw_error_context_t *error);

/** Mount the fixed-to-ESP32 Rev-1 SDMMC interface. */
fw_status_t board_storage_mount(platform_storage_t **storage,
                                fw_error_context_t *error);

#endif /* GEOPHYS_BOARD_REV_1_H */
