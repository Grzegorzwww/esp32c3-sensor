#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "communication.h"

// Tag dla logów
static const char *TAG = "MAIN";

// Licznik uruchomień
static int boot_count = 0;

void app_main(void)
{
    // Inicjalizacja podstawowych systemów
    ESP_ERROR_CHECK(nvs_flash_init());
    
    ESP_LOGI(TAG, "ESP32-C3 startuje...");
    ESP_LOGI(TAG, "Uruchomienie #%d", ++boot_count);
    ESP_LOGI(TAG, "Wolna pamięć: %d bajtów", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Wersja ESP-IDF: %s", esp_get_idf_version());
    
    // Inicjalizacja modułu komunikacji
    ESP_LOGI(TAG, "Inicjalizacja modułu komunikacji...");
    esp_err_t ret = communication_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji komunikacji: %s", esp_err_to_name(ret));
        return;
    }
    
    // Łączenie z WiFi
    ESP_LOGI(TAG, "Łączenie z WiFi...");
    bool wifi_connected = communication_connect_wifi();
    if (wifi_connected) {
        ESP_LOGI(TAG, "Połączono z WiFi!");
        
        // Łączenie z MQTT
        ESP_LOGI(TAG, "Łączenie z MQTT...");
        bool mqtt_connected = communication_connect_mqtt();
        if (mqtt_connected) {
            ESP_LOGI(TAG, "Połączono z MQTT!");
            
            // Test publikowania danych
            char sensor_data[128];
            for (int i = 0; i < 10; i++) {
                // Symulowane dane z czujników
                int temperature = 20 + (i % 10);  // 20-29°C
                int humidity = 60 + (i % 20);     // 60-79%
                int memory = esp_get_free_heap_size();
                
                snprintf(sensor_data, sizeof(sensor_data), 
                        "{\"temp\":%d,\"hum\":%d,\"mem\":%d,\"count\":%d}", 
                        temperature, humidity, memory, i + 1);
                
                ESP_LOGI(TAG, "Test #%d - wysyłanie danych...", i + 1);
                bool published = communication_publish_data("esp32c3/sensor_data", sensor_data);
                
                if (published) {
                    ESP_LOGI(TAG, "✅ Dane wysłane pomyślnie");
                } else {
                    ESP_LOGE(TAG, "❌ Błąd wysyłania danych");
                }
                
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
            
        } else {
            ESP_LOGE(TAG, "Nie udało się połączyć z MQTT!");
        }
        
    } else {
        ESP_LOGE(TAG, "Nie udało się połączyć z WiFi!");
    }
    
    // Cleanup
    communication_cleanup();
    
    ESP_LOGI(TAG, "Program zakończony - restart za 5 sekund");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
