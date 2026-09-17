#ifndef GEOPHYS_PLATFORM_CRC_H
#define GEOPHYS_PLATFORM_CRC_H

#include <stddef.h>
#include <stdint.h>

#define PLATFORM_CRC8_BE_POLYNOMIAL UINT8_C(0x07)
#define PLATFORM_CRC32_LE_POLYNOMIAL UINT32_C(0x04C11DB7)

/**
 * @brief Update an MSB-first CRC-8 remainder.
 *
 * initial_crc is the raw initial or previously returned remainder. No final
 * XOR is applied, so separate buffers can be processed by passing each return
 * value into the next call. data must be non-null when length_bytes is nonzero.
 */
uint8_t platform_crc8_be(uint8_t initial_crc,
                         const uint8_t *data,
                         size_t length_bytes);

/**
 * @brief Update a reflected CRC-32 remainder.
 *
 * initial_crc is the raw initial or previously returned remainder. No final
 * XOR is applied. This is the IEEE CRC-32 polynomial provided by the ESP ROM;
 * it is not CRC-32C (Castagnoli). data must be non-null when length_bytes is
 * nonzero.
 */
uint32_t platform_crc32_le(uint32_t initial_crc,
                           const uint8_t *data,
                           size_t length_bytes);

#endif /* GEOPHYS_PLATFORM_CRC_H */
