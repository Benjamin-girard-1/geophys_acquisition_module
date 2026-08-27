#include "platform_gpio.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "platform_error.h"

static bool s_interrupt_service_initialized;

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
                                          platform_gpio_pull_t pull,
                                          fw_error_context_t *error)
{
    gpio_pullup_t pull_up;
    gpio_pulldown_t pull_down;

    platform_error_clear(error);

    if (!is_valid_input_pin(pin) ||
        !pull_to_idf(pull, &pull_up, &pull_down)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_CONFIGURE, pin, 0U);
    }

    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up,
        .pull_down_en = pull_down,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return platform_error_from_esp_err(
        gpio_config(&config), error, FW_ERROR_RESOURCE_GPIO,
        FW_ERROR_OPERATION_CONFIGURE, pin, 0U);
}

fw_status_t platform_gpio_configure_output(platform_gpio_pin_t pin,
                                           platform_gpio_level_t initial_level,
                                           fw_error_context_t *error)
{
    platform_error_clear(error);

    if (!is_valid_output_pin(pin) || !is_valid_level(initial_level)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_CONFIGURE, pin, (uint32_t)initial_level);
    }

    fw_status_t status = platform_error_from_esp_err(
        gpio_set_level((gpio_num_t)pin, (uint32_t)initial_level), error,
        FW_ERROR_RESOURCE_GPIO, FW_ERROR_OPERATION_CONFIGURE, pin,
        (uint32_t)initial_level);
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

    return platform_error_from_esp_err(
        gpio_config(&config), error, FW_ERROR_RESOURCE_GPIO,
        FW_ERROR_OPERATION_CONFIGURE, pin, (uint32_t)initial_level);
}

fw_status_t platform_gpio_write(platform_gpio_pin_t pin,
                                platform_gpio_level_t level,
                                fw_error_context_t *error)
{
    platform_error_clear(error);

    if (!is_valid_output_pin(pin) || !is_valid_level(level)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_WRITE, pin, (uint32_t)level);
    }

    return platform_error_from_esp_err(
        gpio_set_level((gpio_num_t)pin, (uint32_t)level), error,
        FW_ERROR_RESOURCE_GPIO, FW_ERROR_OPERATION_WRITE, pin,
        (uint32_t)level);
}

fw_status_t platform_gpio_read(platform_gpio_pin_t pin,
                               platform_gpio_level_t *level,
                               fw_error_context_t *error)
{
    platform_error_clear(error);

    if (!is_valid_input_pin(pin) || (level == NULL)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_READ, pin, 0U);
    }

    *level = (gpio_get_level((gpio_num_t)pin) == 0) ?
             PLATFORM_GPIO_LEVEL_LOW : PLATFORM_GPIO_LEVEL_HIGH;
    return FW_STATUS_OK;
}

fw_status_t platform_gpio_interrupt_service_init(fw_error_context_t *error)
{
    platform_error_clear(error);

    if (s_interrupt_service_initialized) {
        return FW_STATUS_OK;
    }

    const fw_status_t status = platform_error_from_esp_err(
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM), error,
        FW_ERROR_RESOURCE_GPIO, FW_ERROR_OPERATION_INITIALIZE,
        FW_ERROR_INSTANCE_NONE, 0U);
    if (status == FW_STATUS_OK) {
        s_interrupt_service_initialized = true;
    }

    return status;
}

fw_status_t platform_gpio_interrupt_attach(platform_gpio_pin_t pin,
                                           platform_gpio_interrupt_t trigger,
                                           platform_gpio_isr_handler_t handler,
                                           void *context,
                                           fw_error_context_t *error)
{
    gpio_int_type_t interrupt_type;

    platform_error_clear(error);

    if (!is_valid_input_pin(pin) || (handler == NULL) ||
        !interrupt_to_idf(trigger, &interrupt_type)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_ATTACH, pin, 0U);
    }
    if (!s_interrupt_service_initialized) {
        return platform_error_set(
            error, FW_STATUS_NOT_INITIALIZED, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_ATTACH, pin, 0U);
    }

    fw_status_t status = platform_error_from_esp_err(
        gpio_intr_disable((gpio_num_t)pin), error, FW_ERROR_RESOURCE_GPIO,
        FW_ERROR_OPERATION_ATTACH, pin, 0U);
    if (status != FW_STATUS_OK) {
        return status;
    }

    status = platform_error_from_esp_err(
        gpio_set_intr_type((gpio_num_t)pin, interrupt_type), error,
        FW_ERROR_RESOURCE_GPIO, FW_ERROR_OPERATION_ATTACH, pin, 0U);
    if (status != FW_STATUS_OK) {
        return status;
    }

    return platform_error_from_esp_err(
        gpio_isr_handler_add((gpio_num_t)pin, handler, context), error,
        FW_ERROR_RESOURCE_GPIO, FW_ERROR_OPERATION_ATTACH, pin, 0U);
}

fw_status_t platform_gpio_interrupt_enable(platform_gpio_pin_t pin,
                                           fw_error_context_t *error)
{
    platform_error_clear(error);

    if (!is_valid_input_pin(pin)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_ENABLE, pin, 0U);
    }
    if (!s_interrupt_service_initialized) {
        return platform_error_set(
            error, FW_STATUS_NOT_INITIALIZED, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_ENABLE, pin, 0U);
    }

    return platform_error_from_esp_err(
        gpio_intr_enable((gpio_num_t)pin), error, FW_ERROR_RESOURCE_GPIO,
        FW_ERROR_OPERATION_ENABLE, pin, 0U);
}

fw_status_t platform_gpio_interrupt_disable(platform_gpio_pin_t pin,
                                            fw_error_context_t *error)
{
    platform_error_clear(error);

    if (!is_valid_input_pin(pin)) {
        return platform_error_set(
            error, FW_STATUS_INVALID_ARGUMENT, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_DISABLE, pin, 0U);
    }
    if (!s_interrupt_service_initialized) {
        return platform_error_set(
            error, FW_STATUS_NOT_INITIALIZED, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_DISABLE, pin, 0U);
    }

    return platform_error_from_esp_err(
        gpio_intr_disable((gpio_num_t)pin), error, FW_ERROR_RESOURCE_GPIO,
        FW_ERROR_OPERATION_DISABLE, pin, 0U);
}

fw_status_t platform_gpio_interrupt_detach(platform_gpio_pin_t pin,
                                           fw_error_context_t *error)
{
    platform_error_clear(error);

    const fw_status_t status = platform_gpio_interrupt_disable(pin, NULL);
    if (status != FW_STATUS_OK) {
        return platform_error_set(
            error, status, FW_ERROR_RESOURCE_GPIO,
            FW_ERROR_OPERATION_DETACH, pin, 0U);
    }

    return platform_error_from_esp_err(
        gpio_isr_handler_remove((gpio_num_t)pin), error,
        FW_ERROR_RESOURCE_GPIO, FW_ERROR_OPERATION_DETACH, pin, 0U);
}
