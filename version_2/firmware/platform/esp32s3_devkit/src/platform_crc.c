#include "platform_crc.h"

#include <stdint.h>

#include "esp_rom_crc.h"

uint8_t platform_crc8_be(uint8_t initial_crc,
                         const uint8_t *data,
                         size_t length_bytes)
{
#if SIZE_MAX > UINT32_MAX
    while (length_bytes > UINT32_MAX) {
        initial_crc = (uint8_t)~esp_rom_crc8_be(
            (uint8_t)~initial_crc, data, UINT32_MAX);
        data += UINT32_MAX;
        length_bytes -= UINT32_MAX;
    }
#endif

    /* ESP ROM complements both its input and output CRC values. */
    return (uint8_t)~esp_rom_crc8_be(
        (uint8_t)~initial_crc, data, (uint32_t)length_bytes);
}

uint32_t platform_crc32_le(uint32_t initial_crc,
                           const uint8_t *data,
                           size_t length_bytes)
{
#if SIZE_MAX > UINT32_MAX
    while (length_bytes > UINT32_MAX) {
        initial_crc = ~esp_rom_crc32_le(
            ~initial_crc, data, UINT32_MAX);
        data += UINT32_MAX;
        length_bytes -= UINT32_MAX;
    }
#endif

    /* Expose a raw, chainable remainder instead of the ROM complement API. */
    return ~esp_rom_crc32_le(
        ~initial_crc, data, (uint32_t)length_bytes);
}
