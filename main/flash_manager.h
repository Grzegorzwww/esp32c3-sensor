// flash_manager.h
#ifndef FLASH_MANAGER_H
#define FLASH_MANAGER_H

#include <esp_err.h>
#include <stddef.h>




esp_err_t flash_manager_init(void);

esp_err_t flash_manager_write(const char *key, const void *data, size_t len);

esp_err_t flash_manager_read(const char *key, void *data, size_t *len);

esp_err_t flash_manager_erase(const char *key);

esp_err_t flash_manager_format(void);

esp_err_t flash_manager_save_string(const char *key, const char *str);

esp_err_t flash_manager_read_string(const char *key, char *str, size_t *len);

esp_err_t flash_manager_save_uint32_t(const char *key, uint32_t value);

esp_err_t flash_manager_read_uint32_t(const char *key, uint32_t *value);



#endif