#include "platform_time.h"

#include "esp_attr.h"
#include "esp_timer.h"

fw_monotonic_us_t platform_monotonic_time_us(void)
{
    return (fw_monotonic_us_t)esp_timer_get_time();
}

fw_monotonic_us_t IRAM_ATTR platform_monotonic_time_us_isr(void)
{
    return (fw_monotonic_us_t)esp_timer_get_time();
}
