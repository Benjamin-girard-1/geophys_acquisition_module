#ifndef GEOPHYS_PLATFORM_STORAGE_H
#define GEOPHYS_PLATFORM_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_error.h"

#define PLATFORM_STORAGE_ENTRY_NAME_SIZE_BYTES UINT16_C(256)

typedef struct platform_storage platform_storage_t;
typedef struct platform_storage_file platform_storage_file_t;
typedef struct platform_storage_directory platform_storage_directory_t;

typedef struct {
    uint32_t clock_pin;
    uint32_t command_pin;
    uint32_t data0_pin;
    uint32_t data1_pin;
    uint32_t data2_pin;
    uint32_t data3_pin;
    uint32_t clock_hz;
    uint8_t bus_width;
} platform_storage_config_t;

typedef struct {
    char name[PLATFORM_STORAGE_ENTRY_NAME_SIZE_BYTES];
    uint64_t size_bytes;
    bool is_regular_file;
} platform_storage_entry_t;

/** Mount one FAT-formatted SD card without ever formatting it implicitly. */
fw_status_t platform_storage_mount(
    const platform_storage_config_t *config,
    platform_storage_t **storage,
    fw_error_context_t *error);

fw_status_t platform_storage_unmount(platform_storage_t *storage,
                                     fw_error_context_t *error);

fw_status_t platform_storage_make_directory(
    platform_storage_t *storage,
    const char *relative_path,
    fw_error_context_t *error);

fw_status_t platform_storage_file_open_new(
    platform_storage_t *storage,
    const char *relative_path,
    platform_storage_file_t **file,
    fw_error_context_t *error);

fw_status_t platform_storage_file_open_read(
    platform_storage_t *storage,
    const char *relative_path,
    platform_storage_file_t **file,
    fw_error_context_t *error);

fw_status_t platform_storage_file_read(
    platform_storage_file_t *file,
    uint32_t offset_bytes,
    uint8_t *data,
    size_t capacity_bytes,
    size_t *bytes_read,
    fw_error_context_t *error);

fw_status_t platform_storage_file_write(
    platform_storage_file_t *file,
    const uint8_t *data,
    size_t length_bytes,
    fw_error_context_t *error);

fw_status_t platform_storage_file_sync(platform_storage_file_t *file,
                                       fw_error_context_t *error);

fw_status_t platform_storage_file_truncate(platform_storage_file_t *file,
                                           uint32_t size_bytes,
                                           fw_error_context_t *error);

fw_status_t platform_storage_file_close(platform_storage_file_t *file,
                                        fw_error_context_t *error);

fw_status_t platform_storage_file_stat(
    platform_storage_t *storage,
    const char *relative_path,
    uint64_t *size_bytes,
    bool *is_regular_file,
    fw_error_context_t *error);

fw_status_t platform_storage_file_delete(
    platform_storage_t *storage,
    const char *relative_path,
    fw_error_context_t *error);

fw_status_t platform_storage_directory_open(
    platform_storage_t *storage,
    const char *relative_path,
    platform_storage_directory_t **directory,
    fw_error_context_t *error);

/** Return NOT_FOUND at the end of the directory. */
fw_status_t platform_storage_directory_read(
    platform_storage_directory_t *directory,
    platform_storage_entry_t *entry,
    fw_error_context_t *error);

fw_status_t platform_storage_directory_close(
    platform_storage_directory_t *directory,
    fw_error_context_t *error);

#endif /* GEOPHYS_PLATFORM_STORAGE_H */
