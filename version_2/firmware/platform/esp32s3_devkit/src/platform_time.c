#include "platform_time.h"

#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

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
