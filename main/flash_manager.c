
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "config.h"
#include "flash_manager.h"


esp_err_t flash_manager_init(void)
{

    return nvs_flash_init();

}

esp_err_t flash_manager_write(const char *key, const void *data, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, key, data, len);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to write blob to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to commit changes to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;



}

esp_err_t flash_manager_read(const char *key, void *data, size_t *len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_blob(handle, key, data, len);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to read blob from NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t flash_manager_erase(const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to erase key from NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t flash_manager_format(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to erase all keys from NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t flash_manager_save_string(const char *key, const char *str)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, key, str);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to write string to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to commit changes to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t flash_manager_read_string(const char *key, char *str, size_t *len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_str(handle, key, str, len);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to read string from NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t flash_manager_save_uint32_t(const char *key, uint32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to write uint32 to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to commit changes to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t flash_manager_read_uint32_t(const char *key, uint32_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_u32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE("FLASH_MANAGER", "Failed to read uint32 from NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}
