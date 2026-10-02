#include "platform_memory.h"

#include <stdint.h>

#include "esp_heap_caps.h"

void *platform_memory_allocate_external(size_t size_bytes,
                                        size_t alignment_bytes)
{
    if (size_bytes == 0U || alignment_bytes == 0U ||
        (alignment_bytes & (alignment_bytes - 1U)) != 0U) {
        return NULL;
    }
    return heap_caps_aligned_alloc(
        alignment_bytes, size_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void platform_memory_free(void *memory)
{
    heap_caps_free(memory);
}
