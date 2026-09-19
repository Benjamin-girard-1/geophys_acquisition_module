#include "platform_storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "platform_error.h"
#include "sdmmc_cmd.h"

#define PLATFORM_STORAGE_MOUNT_POINT "/sdcard"
#define PLATFORM_STORAGE_PATH_SIZE_BYTES UINT16_C(320)
#define PLATFORM_STORAGE_INSTANCE UINT32_C(0)

struct platform_storage {
    sdmmc_card_t *card;
    bool mounted;
};

struct platform_storage_file {
    int descriptor;
    bool open;
};

struct platform_storage_directory {
    DIR *handle;
    char absolute_path[PLATFORM_STORAGE_PATH_SIZE_BYTES];
    bool open;
};

static struct platform_storage s_storage;
static struct platform_storage_file s_file;
static struct platform_storage_directory s_directory;

static fw_status_t storage_error(fw_error_context_t *error,
                                 fw_status_t status,
                                 fw_error_operation_t operation,
                                 uint32_t detail)
{
    return platform_error_set(error, status, FW_ERROR_RESOURCE_STORAGE,
                              operation, PLATFORM_STORAGE_INSTANCE, detail);
}

static fw_status_t errno_status(int value)
{
    switch (value) {
    case 0:
        return FW_STATUS_OK;
    case EINVAL:
        return FW_STATUS_INVALID_ARGUMENT;
    case ENOENT:
        return FW_STATUS_NOT_FOUND;
    case EEXIST:
        return FW_STATUS_ALREADY_EXISTS;
    case EBUSY:
        return FW_STATUS_BUSY;
    case ENOSPC:
    case EFBIG:
        return FW_STATUS_STORAGE_FULL;
    case ENODEV:
    case ENXIO:
        return FW_STATUS_MEDIA_ABSENT;
    default:
        return FW_STATUS_IO;
    }
}

static fw_status_t errno_error(fw_error_context_t *error,
                               fw_error_operation_t operation,
                               int value)
{
    return storage_error(error, errno_status(value), operation,
                         (uint32_t)value);
}

static bool config_is_valid(const platform_storage_config_t *config)
{
    return config != NULL && config->clock_hz != 0U &&
           config->bus_width == 4U;
}

static fw_status_t require_mounted(platform_storage_t *storage,
                                   fw_error_operation_t operation,
                                   fw_error_context_t *error)
{
    if (storage == NULL || storage != &s_storage || !storage->mounted) {
        return storage_error(error, FW_STATUS_NOT_INITIALIZED,
                             operation, 0U);
    }
    return FW_STATUS_OK;
}

static fw_status_t absolute_path(platform_storage_t *storage,
                                 const char *relative_path,
                                 char destination[PLATFORM_STORAGE_PATH_SIZE_BYTES],
                                 fw_error_operation_t operation,
                                 fw_error_context_t *error)
{
    fw_status_t status = require_mounted(storage, operation, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (relative_path == NULL || relative_path[0] != '/' ||
        strstr(relative_path, "..") != NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             operation, 0U);
    }
    const int length = snprintf(destination,
                                PLATFORM_STORAGE_PATH_SIZE_BYTES,
                                "%s%s", PLATFORM_STORAGE_MOUNT_POINT,
                                relative_path);
    if (length < 0 || length >= PLATFORM_STORAGE_PATH_SIZE_BYTES) {
        return storage_error(error, FW_STATUS_OVERFLOW, operation,
                             (length < 0) ? 0U : (uint32_t)length);
    }
    return FW_STATUS_OK;
}

fw_status_t platform_storage_mount(
    const platform_storage_config_t *config,
    platform_storage_t **storage,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (!config_is_valid(config) || storage == NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_MOUNT, 0U);
    }
    *storage = NULL;
    if (s_storage.mounted) {
        *storage = &s_storage;
        return FW_STATUS_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = config->clock_hz / UINT32_C(1000);
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = (gpio_num_t)config->clock_pin;
    slot.cmd = (gpio_num_t)config->command_pin;
    slot.d0 = (gpio_num_t)config->data0_pin;
    slot.d1 = (gpio_num_t)config->data1_pin;
    slot.d2 = (gpio_num_t)config->data2_pin;
    slot.d3 = (gpio_num_t)config->data3_pin;
    slot.width = config->bus_width;
    slot.flags = 0U;

    const esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 0U,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };
    sdmmc_card_t *card = NULL;
    const esp_err_t result = esp_vfs_fat_sdmmc_mount(
        PLATFORM_STORAGE_MOUNT_POINT, &host, &slot, &mount, &card);
    if (result != ESP_OK) {
        const fw_status_t status = (result == ESP_ERR_NO_MEM) ?
            FW_STATUS_INTERNAL : FW_STATUS_MEDIA_ABSENT;
        return storage_error(error, status, FW_ERROR_OPERATION_MOUNT,
                             (uint32_t)result);
    }

    memset(&s_storage, 0, sizeof(s_storage));
    s_storage.card = card;
    s_storage.mounted = true;
    *storage = &s_storage;
    return FW_STATUS_OK;
}

fw_status_t platform_storage_unmount(platform_storage_t *storage,
                                     fw_error_context_t *error)
{
    platform_error_clear(error);
    fw_status_t status = require_mounted(
        storage, FW_ERROR_OPERATION_DEINITIALIZE, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (s_file.open || s_directory.open) {
        return storage_error(error, FW_STATUS_BUSY,
                             FW_ERROR_OPERATION_DEINITIALIZE, 0U);
    }
    const esp_err_t result = esp_vfs_fat_sdcard_unmount(
        PLATFORM_STORAGE_MOUNT_POINT, storage->card);
    if (result != ESP_OK) {
        return platform_error_from_esp_err(
            result, error, FW_ERROR_RESOURCE_STORAGE,
            FW_ERROR_OPERATION_DEINITIALIZE, PLATFORM_STORAGE_INSTANCE, 0U);
    }
    memset(&s_storage, 0, sizeof(s_storage));
    return FW_STATUS_OK;
}

fw_status_t platform_storage_make_directory(
    platform_storage_t *storage,
    const char *relative_path,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    char path[PLATFORM_STORAGE_PATH_SIZE_BYTES];
    fw_status_t status = absolute_path(
        storage, relative_path, path, FW_ERROR_OPERATION_OPEN, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    if (mkdir(path, 0775) == 0) {
        return FW_STATUS_OK;
    }
    if (errno == EEXIST) {
        struct stat information;
        if (stat(path, &information) == 0 && S_ISDIR(information.st_mode)) {
            return FW_STATUS_OK;
        }
    }
    return errno_error(error, FW_ERROR_OPERATION_OPEN, errno);
}

fw_status_t platform_storage_file_open_new(
    platform_storage_t *storage,
    const char *relative_path,
    platform_storage_file_t **file,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (file == NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_OPEN, 0U);
    }
    *file = NULL;
    if (s_file.open) {
        return storage_error(error, FW_STATUS_BUSY,
                             FW_ERROR_OPERATION_OPEN, 0U);
    }
    char path[PLATFORM_STORAGE_PATH_SIZE_BYTES];
    fw_status_t status = absolute_path(
        storage, relative_path, path, FW_ERROR_OPERATION_OPEN, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (descriptor < 0) {
        return errno_error(error, FW_ERROR_OPERATION_OPEN, errno);
    }
    s_file.descriptor = descriptor;
    s_file.open = true;
    *file = &s_file;
    return FW_STATUS_OK;
}

fw_status_t platform_storage_file_open_read(
    platform_storage_t *storage,
    const char *relative_path,
    platform_storage_file_t **file,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (file == NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_OPEN, 0U);
    }
    *file = NULL;
    if (s_file.open) {
        return storage_error(error, FW_STATUS_BUSY,
                             FW_ERROR_OPERATION_OPEN, 0U);
    }
    char path[PLATFORM_STORAGE_PATH_SIZE_BYTES];
    fw_status_t status = absolute_path(
        storage, relative_path, path, FW_ERROR_OPERATION_OPEN, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    const int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        return errno_error(error, FW_ERROR_OPERATION_OPEN, errno);
    }
    s_file.descriptor = descriptor;
    s_file.open = true;
    *file = &s_file;
    return FW_STATUS_OK;
}

fw_status_t platform_storage_file_read(
    platform_storage_file_t *file,
    uint32_t offset_bytes,
    uint8_t *data,
    size_t capacity_bytes,
    size_t *bytes_read,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (file == NULL || file != &s_file || !file->open || data == NULL ||
        capacity_bytes == 0U || bytes_read == NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_READ, offset_bytes);
    }
    *bytes_read = 0U;
    const off_t position = lseek(file->descriptor, (off_t)offset_bytes,
                                 SEEK_SET);
    if (position < 0 || (uint64_t)position != offset_bytes) {
        return (position < 0) ?
            errno_error(error, FW_ERROR_OPERATION_READ, errno) :
            storage_error(error, FW_STATUS_IO, FW_ERROR_OPERATION_READ,
                          offset_bytes);
    }

    while (*bytes_read < capacity_bytes) {
        const ssize_t count = read(file->descriptor, data + *bytes_read,
                                   capacity_bytes - *bytes_read);
        if (count < 0) {
            return errno_error(error, FW_ERROR_OPERATION_READ, errno);
        }
        if (count == 0) {
            break;
        }
        *bytes_read += (size_t)count;
    }
    return FW_STATUS_OK;
}

fw_status_t platform_storage_file_write(
    platform_storage_file_t *file,
    const uint8_t *data,
    size_t length_bytes,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (file == NULL || file != &s_file || !file->open || data == NULL ||
        length_bytes == 0U) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_WRITE, 0U);
    }
    size_t offset = 0U;
    while (offset < length_bytes) {
        const ssize_t written = write(file->descriptor, data + offset,
                                      length_bytes - offset);
        if (written < 0) {
            return errno_error(error, FW_ERROR_OPERATION_WRITE, errno);
        }
        if (written == 0) {
            return storage_error(error, FW_STATUS_IO,
                                 FW_ERROR_OPERATION_WRITE,
                                 (uint32_t)offset);
        }
        offset += (size_t)written;
    }
    return FW_STATUS_OK;
}

fw_status_t platform_storage_file_sync(platform_storage_file_t *file,
                                       fw_error_context_t *error)
{
    platform_error_clear(error);
    if (file == NULL || file != &s_file || !file->open) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_SYNC, 0U);
    }
    return (fsync(file->descriptor) == 0) ? FW_STATUS_OK :
           errno_error(error, FW_ERROR_OPERATION_SYNC, errno);
}

fw_status_t platform_storage_file_truncate(platform_storage_file_t *file,
                                           uint32_t size_bytes,
                                           fw_error_context_t *error)
{
    platform_error_clear(error);
    if (file == NULL || file != &s_file || !file->open) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_WRITE, size_bytes);
    }
    return (ftruncate(file->descriptor, (off_t)size_bytes) == 0) ?
           FW_STATUS_OK :
           errno_error(error, FW_ERROR_OPERATION_WRITE, errno);
}

fw_status_t platform_storage_file_close(platform_storage_file_t *file,
                                        fw_error_context_t *error)
{
    platform_error_clear(error);
    if (file == NULL || file != &s_file || !file->open) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_CLOSE, 0U);
    }
    const int descriptor = file->descriptor;
    memset(&s_file, 0, sizeof(s_file));
    return (close(descriptor) == 0) ? FW_STATUS_OK :
           errno_error(error, FW_ERROR_OPERATION_CLOSE, errno);
}

fw_status_t platform_storage_file_stat(
    platform_storage_t *storage,
    const char *relative_path,
    uint64_t *size_bytes,
    bool *is_regular_file,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (size_bytes == NULL || is_regular_file == NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_READ, 0U);
    }
    *size_bytes = 0U;
    *is_regular_file = false;
    char path[PLATFORM_STORAGE_PATH_SIZE_BYTES];
    fw_status_t status = absolute_path(
        storage, relative_path, path, FW_ERROR_OPERATION_READ, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    struct stat information;
    if (stat(path, &information) != 0) {
        return errno_error(error, FW_ERROR_OPERATION_READ, errno);
    }
    *is_regular_file = S_ISREG(information.st_mode);
    *size_bytes = (information.st_size < 0) ? 0U :
                  (uint64_t)information.st_size;
    return FW_STATUS_OK;
}

fw_status_t platform_storage_file_delete(
    platform_storage_t *storage,
    const char *relative_path,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    char path[PLATFORM_STORAGE_PATH_SIZE_BYTES];
    fw_status_t status = absolute_path(
        storage, relative_path, path, FW_ERROR_OPERATION_DELETE, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    return (unlink(path) == 0) ? FW_STATUS_OK :
           errno_error(error, FW_ERROR_OPERATION_DELETE, errno);
}

fw_status_t platform_storage_directory_open(
    platform_storage_t *storage,
    const char *relative_path,
    platform_storage_directory_t **directory,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (directory == NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_OPEN, 0U);
    }
    *directory = NULL;
    if (s_directory.open) {
        return storage_error(error, FW_STATUS_BUSY,
                             FW_ERROR_OPERATION_OPEN, 0U);
    }
    fw_status_t status = absolute_path(
        storage, relative_path, s_directory.absolute_path,
        FW_ERROR_OPERATION_OPEN, error);
    if (status != FW_STATUS_OK) {
        return status;
    }
    DIR *handle = opendir(s_directory.absolute_path);
    if (handle == NULL) {
        memset(&s_directory, 0, sizeof(s_directory));
        return errno_error(error, FW_ERROR_OPERATION_OPEN, errno);
    }
    s_directory.handle = handle;
    s_directory.open = true;
    *directory = &s_directory;
    return FW_STATUS_OK;
}

fw_status_t platform_storage_directory_read(
    platform_storage_directory_t *directory,
    platform_storage_entry_t *entry,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (directory == NULL || directory != &s_directory ||
        !directory->open || entry == NULL) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_READ, 0U);
    }
    memset(entry, 0, sizeof(*entry));

    for (;;) {
        errno = 0;
        struct dirent *item = readdir(directory->handle);
        if (item == NULL) {
            return (errno == 0) ?
                storage_error(error, FW_STATUS_NOT_FOUND,
                              FW_ERROR_OPERATION_READ, 0U) :
                errno_error(error, FW_ERROR_OPERATION_READ, errno);
        }
        if (strcmp(item->d_name, ".") == 0 ||
            strcmp(item->d_name, "..") == 0) {
            continue;
        }
        const size_t name_length = strlen(item->d_name);
        if (name_length >= sizeof(entry->name)) {
            return storage_error(error, FW_STATUS_OVERFLOW,
                                 FW_ERROR_OPERATION_READ,
                                 (uint32_t)name_length);
        }
        memcpy(entry->name, item->d_name, name_length + 1U);

        char path[PLATFORM_STORAGE_PATH_SIZE_BYTES];
        const int length = snprintf(path, sizeof(path), "%s/%s",
                                    directory->absolute_path,
                                    item->d_name);
        if (length < 0 || length >= (int)sizeof(path)) {
            return storage_error(error, FW_STATUS_OVERFLOW,
                                 FW_ERROR_OPERATION_READ,
                                 (length < 0) ? 0U : (uint32_t)length);
        }
        struct stat information;
        if (stat(path, &information) != 0) {
            return errno_error(error, FW_ERROR_OPERATION_READ, errno);
        }
        entry->is_regular_file = S_ISREG(information.st_mode);
        entry->size_bytes = (information.st_size < 0) ? 0U :
                            (uint64_t)information.st_size;
        return FW_STATUS_OK;
    }
}

fw_status_t platform_storage_directory_close(
    platform_storage_directory_t *directory,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (directory == NULL || directory != &s_directory ||
        !directory->open) {
        return storage_error(error, FW_STATUS_INVALID_ARGUMENT,
                             FW_ERROR_OPERATION_CLOSE, 0U);
    }
    DIR *handle = directory->handle;
    memset(&s_directory, 0, sizeof(s_directory));
    return (closedir(handle) == 0) ? FW_STATUS_OK :
           errno_error(error, FW_ERROR_OPERATION_CLOSE, errno);
}
