#include "platform_gpio.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"

static bool s_interrupt_service_initialized;

static fw_status_t status_from_esp_err(esp_err_t result)
{
    switch (result) {
    case ESP_OK:
        return FW_STATUS_OK;
    case ESP_ERR_INVALID_ARG:
        return FW_STATUS_INVALID_ARGUMENT;
    case ESP_ERR_INVALID_STATE:
        return FW_STATUS_INVALID_STATE;
    case ESP_ERR_NOT_FOUND:
        return FW_STATUS_NOT_FOUND;
    case ESP_ERR_NO_MEM:
        return FW_STATUS_INTERNAL;
    default:
        return FW_STATUS_IO;
    }
}

static bool is_valid_input_pin(platform_gpio_pin_t pin)
{
    return (pin < (platform_gpio_pin_t)GPIO_NUM_MAX) &&
           GPIO_IS_VALID_GPIO((gpio_num_t)pin);
}

static bool is_valid_output_pin(platform_gpio_pin_t pin)
{
    return (pin < (platform_gpio_pin_t)GPIO_NUM_MAX) &&
           GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin);
}

static bool is_valid_level(platform_gpio_level_t level)
{
    return (level == PLATFORM_GPIO_LEVEL_LOW) ||
           (level == PLATFORM_GPIO_LEVEL_HIGH);
}

static bool pull_to_idf(platform_gpio_pull_t pull,
                        gpio_pullup_t *pull_up,
                        gpio_pulldown_t *pull_down)
{
    switch (pull) {
    case PLATFORM_GPIO_PULL_NONE:
        *pull_up = GPIO_PULLUP_DISABLE;
        *pull_down = GPIO_PULLDOWN_DISABLE;
        return true;
    case PLATFORM_GPIO_PULL_UP:
        *pull_up = GPIO_PULLUP_ENABLE;
        *pull_down = GPIO_PULLDOWN_DISABLE;
        return true;
    case PLATFORM_GPIO_PULL_DOWN:
        *pull_up = GPIO_PULLUP_DISABLE;
        *pull_down = GPIO_PULLDOWN_ENABLE;
        return true;
    default:
        return false;
    }
}

static bool interrupt_to_idf(platform_gpio_interrupt_t trigger,
                             gpio_int_type_t *interrupt_type)
{
    switch (trigger) {
    case PLATFORM_GPIO_INTERRUPT_RISING_EDGE:
        *interrupt_type = GPIO_INTR_POSEDGE;
        return true;
    case PLATFORM_GPIO_INTERRUPT_FALLING_EDGE:
        *interrupt_type = GPIO_INTR_NEGEDGE;
        return true;
    case PLATFORM_GPIO_INTERRUPT_ANY_EDGE:
        *interrupt_type = GPIO_INTR_ANYEDGE;
        return true;
    default:
        return false;
    }
}

fw_status_t platform_gpio_configure_input(platform_gpio_pin_t pin,
                                          platform_gpio_pull_t pull)
{
    gpio_pullup_t pull_up;
    gpio_pulldown_t pull_down;

    if (!is_valid_input_pin(pin) ||
        !pull_to_idf(pull, &pull_up, &pull_down)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up,
        .pull_down_en = pull_down,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return status_from_esp_err(gpio_config(&config));
}

fw_status_t platform_gpio_configure_output(platform_gpio_pin_t pin,
                                           platform_gpio_level_t initial_level)
{
    if (!is_valid_output_pin(pin) || !is_valid_level(initial_level)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    fw_status_t status = status_from_esp_err(
        gpio_set_level((gpio_num_t)pin, (uint32_t)initial_level));
    if (status != FW_STATUS_OK) {
        return status;
    }

    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return status_from_esp_err(gpio_config(&config));
}

fw_status_t platform_gpio_write(platform_gpio_pin_t pin,
                                platform_gpio_level_t level)
{
    if (!is_valid_output_pin(pin) || !is_valid_level(level)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    return status_from_esp_err(
        gpio_set_level((gpio_num_t)pin, (uint32_t)level));
}

fw_status_t platform_gpio_read(platform_gpio_pin_t pin,
                               platform_gpio_level_t *level)
{
    if (!is_valid_input_pin(pin) || (level == NULL)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }

    *level = (gpio_get_level((gpio_num_t)pin) == 0) ?
             PLATFORM_GPIO_LEVEL_LOW : PLATFORM_GPIO_LEVEL_HIGH;
    return FW_STATUS_OK;
}

fw_status_t platform_gpio_interrupt_service_init(void)
{
    if (s_interrupt_service_initialized) {
        return FW_STATUS_OK;
    }

    const fw_status_t status = status_from_esp_err(
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    if (status == FW_STATUS_OK) {
        s_interrupt_service_initialized = true;
    }

    return status;
}

fw_status_t platform_gpio_interrupt_attach(platform_gpio_pin_t pin,
                                           platform_gpio_interrupt_t trigger,
                                           platform_gpio_isr_handler_t handler,
                                           void *context)
{
    gpio_int_type_t interrupt_type;

    if (!is_valid_input_pin(pin) || (handler == NULL) ||
        !interrupt_to_idf(trigger, &interrupt_type)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    if (!s_interrupt_service_initialized) {
        return FW_STATUS_NOT_INITIALIZED;
    }

    fw_status_t status = status_from_esp_err(
        gpio_intr_disable((gpio_num_t)pin));
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = status_from_esp_err(
        gpio_set_intr_type((gpio_num_t)pin, interrupt_type));
    if (status != FW_STATUS_OK) {
        return status;
    }

    return status_from_esp_err(
        gpio_isr_handler_add((gpio_num_t)pin, handler, context));
}

fw_status_t platform_gpio_interrupt_enable(platform_gpio_pin_t pin)
{
    if (!is_valid_input_pin(pin)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    if (!s_interrupt_service_initialized) {
        return FW_STATUS_NOT_INITIALIZED;
    }

    return status_from_esp_err(gpio_intr_enable((gpio_num_t)pin));
}

fw_status_t platform_gpio_interrupt_disable(platform_gpio_pin_t pin)
{
    if (!is_valid_input_pin(pin)) {
        return FW_STATUS_INVALID_ARGUMENT;
    }
    if (!s_interrupt_service_initialized) {
        return FW_STATUS_NOT_INITIALIZED;
    }

    return status_from_esp_err(gpio_intr_disable((gpio_num_t)pin));
}

fw_status_t platform_gpio_interrupt_detach(platform_gpio_pin_t pin)
{
    const fw_status_t status = platform_gpio_interrupt_disable(pin);
    if (status != FW_STATUS_OK) {
        return status;
    }

    return status_from_esp_err(gpio_isr_handler_remove((gpio_num_t)pin));
}
