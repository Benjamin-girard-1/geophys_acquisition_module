#include "platform_time.h"

#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

fw_monotonic_us_t platform_monotonic_time_us(void)
{
    return (fw_monotonic_us_t)esp_timer_get_time();
}

fw_monotonic_us_t IRAM_ATTR platform_monotonic_time_us_isr(void)
{
    return (fw_monotonic_us_t)esp_timer_get_time();
}

void platform_delay_us(uint32_t duration_us)
{
    esp_rom_delay_us(duration_us);
}

void platform_delay_ms(uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return;
    }

    const uint64_t ticks =
        (((uint64_t)duration_ms * (uint64_t)configTICK_RATE_HZ) +
         UINT64_C(999)) /
        UINT64_C(1000);
    vTaskDelay((ticks >= (uint64_t)portMAX_DELAY) ?
               (portMAX_DELAY - 1U) : (TickType_t)ticks);
}
