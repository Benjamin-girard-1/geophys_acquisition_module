#ifndef GEOPHYS_PLATFORM_MEMORY_H
#define GEOPHYS_PLATFORM_MEMORY_H

#include <stddef.h>

/** Allocate byte-addressable external RAM with the requested alignment. */
void *platform_memory_allocate_external(size_t size_bytes,
                                        size_t alignment_bytes);

/** Release memory returned by platform_memory_allocate_external(). */
void platform_memory_free(void *memory);

#endif /* GEOPHYS_PLATFORM_MEMORY_H */
