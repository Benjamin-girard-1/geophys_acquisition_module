#include "platform_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "platform_error.h"

#define PLATFORM_SETTINGS_INSTANCE UINT32_C(1)

static bool s_initialized;

static fw_status_t settings_error(fw_error_context_t *error,
                                  fw_status_t status,
                                  fw_error_operation_t operation,
                                  uint32_t detail)
{
    return platform_error_set(error, status, FW_ERROR_RESOURCE_STORAGE,
                              operation, PLATFORM_SETTINGS_INSTANCE, detail);
}

static fw_status_t nvs_status(esp_err_t result)
{
    switch (result) {
    case ESP_OK:
        return FW_STATUS_OK;
    case ESP_ERR_INVALID_ARG:
        return FW_STATUS_INVALID_ARGUMENT;
    case ESP_ERR_NVS_NOT_FOUND:
        return FW_STATUS_NOT_FOUND;
    case ESP_ERR_NVS_INVALID_LENGTH:
        return FW_STATUS_OVERFLOW;
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
    case ESP_ERR_NVS_NO_FREE_PAGES:
        return FW_STATUS_STORAGE_FULL;
    case ESP_ERR_NO_MEM:
        return FW_STATUS_INTERNAL;
    default:
        return FW_STATUS_IO;
    }
}

static fw_status_t nvs_error(fw_error_context_t *error,
                             esp_err_t result,
                             fw_error_operation_t operation,
                             uint32_t detail)
{
    return settings_error(error, nvs_status(result), operation, detail);
}

static bool name_is_valid(const char *name)
{
    return name != NULL && name[0] != '\0';
}

fw_status_t platform_settings_initialize(fw_error_context_t *error)
{
    platform_error_clear(error);
    if (s_initialized) {
        return FW_STATUS_OK;
    }

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result == ESP_OK) {
            result = nvs_flash_init();
        }
    }
    if (result != ESP_OK) {
        return nvs_error(error, result, FW_ERROR_OPERATION_INITIALIZE, 0U);
    }

    s_initialized = true;
    return FW_STATUS_OK;
}

fw_status_t platform_settings_read_blob(
    const char *namespace_name,
    const char *key,
    uint8_t *data,
    size_t capacity_bytes,
    size_t *length_bytes,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (!name_is_valid(namespace_name) || !name_is_valid(key) ||
        data == NULL || capacity_bytes == 0U || length_bytes == NULL) {
        return settings_error(error, FW_STATUS_INVALID_ARGUMENT,
                              FW_ERROR_OPERATION_READ, 0U);
    }
    *length_bytes = 0U;

    fw_status_t status = platform_settings_initialize(error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return nvs_error(error, result, FW_ERROR_OPERATION_OPEN, 0U);
    }

    size_t stored_bytes = 0U;
    result = nvs_get_blob(handle, key, NULL, &stored_bytes);
    if (result != ESP_OK) {
        nvs_close(handle);
        return nvs_error(error, result, FW_ERROR_OPERATION_READ, 0U);
    }
    *length_bytes = stored_bytes;
    if (stored_bytes > capacity_bytes) {
        nvs_close(handle);
        const uint32_t detail = (stored_bytes > UINT32_MAX) ?
            UINT32_MAX : (uint32_t)stored_bytes;
        return settings_error(error, FW_STATUS_OVERFLOW,
                              FW_ERROR_OPERATION_READ, detail);
    }

    size_t read_bytes = stored_bytes;
    result = nvs_get_blob(handle, key, data, &read_bytes);
    nvs_close(handle);
    if (result != ESP_OK) {
        return nvs_error(error, result, FW_ERROR_OPERATION_READ, 0U);
    }
    *length_bytes = read_bytes;
    return FW_STATUS_OK;
}

fw_status_t platform_settings_write_blob(
    const char *namespace_name,
    const char *key,
    const uint8_t *data,
    size_t length_bytes,
    fw_error_context_t *error)
{
    platform_error_clear(error);
    if (!name_is_valid(namespace_name) || !name_is_valid(key) ||
        data == NULL || length_bytes == 0U) {
        return settings_error(error, FW_STATUS_INVALID_ARGUMENT,
                              FW_ERROR_OPERATION_WRITE, 0U);
    }

    fw_status_t status = platform_settings_initialize(error);
    if (status != FW_STATUS_OK) {
        return status;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return nvs_error(error, result, FW_ERROR_OPERATION_OPEN, 0U);
    }

    result = nvs_set_blob(handle, key, data, length_bytes);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result != ESP_OK) {
        return nvs_error(error, result, FW_ERROR_OPERATION_WRITE, 0U);
    }
    return FW_STATUS_OK;
}
