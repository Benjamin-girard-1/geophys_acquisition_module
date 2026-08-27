#ifndef GEOPHYS_PLATFORM_GPIO_H
#define GEOPHYS_PLATFORM_GPIO_H

#include <stdint.h>

#include "fw_status.h"

typedef uint32_t platform_gpio_pin_t;

typedef enum {
    PLATFORM_GPIO_LEVEL_LOW = 0,
    PLATFORM_GPIO_LEVEL_HIGH,
} platform_gpio_level_t;

typedef enum {
    PLATFORM_GPIO_PULL_NONE = 0,
    PLATFORM_GPIO_PULL_UP,
    PLATFORM_GPIO_PULL_DOWN,
} platform_gpio_pull_t;

typedef enum {
    PLATFORM_GPIO_INTERRUPT_RISING_EDGE = 0,
    PLATFORM_GPIO_INTERRUPT_FALLING_EDGE,
    PLATFORM_GPIO_INTERRUPT_ANY_EDGE,
} platform_gpio_interrupt_t;

/**
 * @brief Per-pin interrupt callback invoked in ISR context.
 *
 * When the ISR service uses IRAM, the callback and everything it accesses must
 * also be ISR- and IRAM-safe. It must not block, allocate, or log.
 */
typedef void (*platform_gpio_isr_handler_t)(void *context);

fw_status_t platform_gpio_configure_input(platform_gpio_pin_t pin,
                                          platform_gpio_pull_t pull);

/**
 * @brief Configure an output without producing an unintended initial pulse.
 *
 * The output latch is loaded with initial_level before the output driver is
 * enabled.
 */
fw_status_t platform_gpio_configure_output(platform_gpio_pin_t pin,
                                           platform_gpio_level_t initial_level);

fw_status_t platform_gpio_write(platform_gpio_pin_t pin,
                                platform_gpio_level_t level);

fw_status_t platform_gpio_read(platform_gpio_pin_t pin,
                               platform_gpio_level_t *level);

/**
 * @brief Install the process-wide per-pin GPIO ISR service.
 *
 * Call once during startup before application tasks can register interrupts.
 * Repeated successful calls are harmless.
 */
fw_status_t platform_gpio_interrupt_service_init(void);

/**
 * @brief Attach a per-pin handler while leaving that pin's interrupt disabled.
 */
fw_status_t platform_gpio_interrupt_attach(platform_gpio_pin_t pin,
                                           platform_gpio_interrupt_t trigger,
                                           platform_gpio_isr_handler_t handler,
                                           void *context);

fw_status_t platform_gpio_interrupt_enable(platform_gpio_pin_t pin);
fw_status_t platform_gpio_interrupt_disable(platform_gpio_pin_t pin);
fw_status_t platform_gpio_interrupt_detach(platform_gpio_pin_t pin);

#endif /* GEOPHYS_PLATFORM_GPIO_H */
