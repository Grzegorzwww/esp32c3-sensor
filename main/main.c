#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "lm35.h"

static const char *TAG = "MAIN";
static int boot_count = 0;

void app_main(void)
{
    // Inicjalizacja podstawowych systemów
    ESP_ERROR_CHECK(nvs_flash_init());
    
    ESP_LOGI(TAG, "LM35 Temperature Sensor - Boot #%d", ++boot_count);
    
    // Konfiguracja LM35 - użyj domyślnej konfiguracji
    lm35_config_t config = {
        .adc_unit = ADC_UNIT_1,
        .adc_channel = ADC_CHANNEL_2,    // GPIO2
        .attenuation = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .voltage_reference_mv = 3300.0f
    };
    
    // Inicjalizacja czujnika LM35
    esp_err_t ret = lm35_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LM35 INIT ERROR - Check GPIO2 connection");
        return;
    }
    
    ESP_LOGI(TAG, "LM35 Ready - Reading every 5s...");
    
    // Główna pętla pomiarów
    int measurement_counter = 0;
    lm35_data_t temperature_data;
    
    while (1) {
        measurement_counter++;
        
        // Odczyt temperatury z LM35
        ret = lm35_read_temperature(&temperature_data);
        
        if (ret == ESP_OK && temperature_data.valid) {
            ESP_LOGI(TAG, "#%d: Temp: %.1f C", measurement_counter, temperature_data.temperature_celsius);
        } else {
            ESP_LOGI(TAG, "#%d: ERROR", measurement_counter);
        }
        
        // Czekaj 5 sekund między pomiarami
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
