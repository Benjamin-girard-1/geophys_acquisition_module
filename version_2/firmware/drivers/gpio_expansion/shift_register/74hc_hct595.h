#ifndef GEOPHYS_74HC_HCT595_H
#define GEOPHYS_74HC_HCT595_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_error.h"

#define HC595_OUTPUT_COUNT 16U

/**
 * @brief Logical control lines of one daisy-chained 74HC595 pair.
 *
 * The board adapter maps these component-level lines to physical GPIOs.
 */
typedef enum {
    HC595_LINE_SERIAL_DATA = 0,
    HC595_LINE_SHIFT_CLOCK,
    HC595_LINE_STORAGE_CLOCK,
    HC595_LINE_OUTPUT_ENABLE_N,
} hc595_line_t;

/**
 * @brief Drive one 74HC595 control line.
 *
 * error is optional. A callback failure may attach lower-layer context such as
 * the physical GPIO and operation that failed.
 */
typedef fw_status_t (*hc595_write_line_callback_t)(
    void *context,
    hc595_line_t line,
    bool high,
    fw_error_context_t *error);

/** @brief Apply a task-context delay between control-line transitions. */
typedef void (*hc595_delay_us_callback_t)(void *context,
                                         uint32_t duration_us);

typedef struct {
    hc595_write_line_callback_t write_line;
    hc595_delay_us_callback_t delay_us;
    void *context;
    uint32_t edge_delay_us;
    uint32_t instance;
} hc595_config_t;

/**
 * @brief Caller-owned state for one 16-output 74HC595 chain.
 *
 * Declare this object with zero initialization and do not modify its members
 * directly. The driver copies the configuration and performs no allocation.
 * Access must be serialized by the owning board module.
 */
typedef struct {
    hc595_config_t config;
    uint16_t shadow;
    bool outputs_enabled;
    bool initialized;
} hc595_t;

/**
 * @brief Bind callbacks and latch an initial complete 16-bit image.
 *
 * OUTPUT_ENABLE_N is driven High before the image is shifted and remains High
 * on success. The board must explicitly enable the outputs after it confirms
 * that the initial image is safe.
 *
 * Bits are shifted least-significant first. For the Rev-1 U1-to-U2 cascade,
 * bit 0 travels through both devices and bit 15 remains at U1 Q0.
 */
fw_status_t hc595_initialize(hc595_t *device,
                             const hc595_config_t *config,
                             uint16_t initial_image,
                             fw_error_context_t *error);

/** @brief Shift and latch one complete 16-bit output image. */
fw_status_t hc595_write_image(hc595_t *device,
                              uint16_t image,
                              fw_error_context_t *error);

/**
 * @brief Atomically clear and set selected shadow bits, then latch all bits.
 *
 * clear_mask and set_mask must not overlap. Unselected bits retain their
 * previous shadow value.
 */
fw_status_t hc595_update_masked(hc595_t *device,
                                uint16_t clear_mask,
                                uint16_t set_mask,
                                fw_error_context_t *error);

/** @brief Set one output by its component-level index from 0 through 15. */
fw_status_t hc595_set_output(hc595_t *device,
                             uint8_t output_index,
                             bool high,
                             fw_error_context_t *error);

/** @brief Return the last image whose storage-clock rising edge succeeded. */
fw_status_t hc595_get_shadow(const hc595_t *device,
                             uint16_t *image,
                             fw_error_context_t *error);

/**
 * @brief Enable or disable the parallel outputs without changing the shadow.
 */
fw_status_t hc595_set_outputs_enabled(hc595_t *device,
                                      bool enabled,
                                      fw_error_context_t *error);

fw_status_t hc595_get_outputs_enabled(const hc595_t *device,
                                      bool *enabled,
                                      fw_error_context_t *error);

#endif /* GEOPHYS_74HC_HCT595_H */
