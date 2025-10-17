#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "communication.h"
#include "bme680_wrapper.h"
#include "battery_monitor.h"

// Tag dla logów
static const char *TAG = "MAIN";

// Konfiguracja deep sleep
#define SLEEP_TIME_MINUTES 5
#define DEBUG_DELAY_SECONDS 10
#define SLEEP_TIME_US (SLEEP_TIME_MINUTES * 60 * 1000000ULL)  // 5 minut w mikrosekundach

// Funkcja do określenia jakości powietrza na podstawie oporu gazu
static const char* get_air_quality_description(float gas_resistance, bool gas_valid)
{
    if (!gas_valid) {
        return "Nieznana";
    }
    
    // Klasyfikacja na podstawie oporu gazu w omach
    if (gas_resistance >= 200000) {        // >= 200kΩ
        return "Świeże powietrze";
    } else if (gas_resistance >= 50000) {   // 50kΩ - 200kΩ
        return "Umiarkowane zanieczyszczenie";
    } else if (gas_resistance >= 10000) {   // 10kΩ - 50kΩ
        return "Wysokie zanieczyszczenie";
    } else {                               // < 10kΩ
        return "Bardzo zanieczyszczone";
    }
}

void app_main(void)
{
    // Inicjalizacja podstawowych systemów
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Sprawdź przyczynę wybudzenia
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "🕐 Wybudzenie przez timer (cykl %d min)", SLEEP_TIME_MINUTES);
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(TAG, "🔄 Pierwsze uruchomienie / reset");
            break;
    }
    
    ESP_LOGI(TAG, "ESP32-C3 startuje...");
    ESP_LOGI(TAG, "Wolna pamięć: %d bajtów", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Wersja ESP-IDF: %s", esp_get_idf_version());
    
    // Inicjalizacja czujnika BME680
    ESP_LOGI(TAG, "Inicjalizacja czujnika BME680...");
    esp_err_t ret = bme680_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji BME680: %s", esp_err_to_name(ret));
        return;
    }
    
    // Inicjalizacja monitora baterii
    ESP_LOGI(TAG, "Inicjalizacja monitora baterii...");
    ret = battery_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji monitora baterii: %s", esp_err_to_name(ret));
        bme680_cleanup();
        return;
    }
    
    // Inicjalizacja modułu komunikacji
    ESP_LOGI(TAG, "Inicjalizacja modułu komunikacji...");
    ret = communication_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji komunikacji: %s", esp_err_to_name(ret));
        bme680_cleanup();
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
            
            // Rozgrzanie czujnika gazu - 2 pomiary dummy
            ESP_LOGI(TAG, "🔥 Rozgrzewanie czujnika gazu (potrzebne po deep sleep)...");
            bme680_data_t warmup_data;
            for (int warmup = 1; warmup <= 2; warmup++) {
                ESP_LOGI(TAG, "🔥 Rozgrzewanie %d/2...", warmup);
                bme680_read_data(&warmup_data);
                ESP_LOGI(TAG, "   Gas: %.0fΩ %s", warmup_data.gas_resistance, 
                         warmup_data.gas_valid ? "✅" : "❌");
                vTaskDelay(pdMS_TO_TICKS(1000)); // 1 sekunda między rozgrzewaniem
            }
            
            // Główny pomiar po rozgrzaniu
            bme680_data_t bme_data;
            bool data_sent_successfully = false;
            
            ESP_LOGI(TAG, "📊 Wykonywanie głównego pomiaru po rozgrzaniu...");
            
            esp_err_t read_ret = bme680_read_data(&bme_data);
            if (read_ret == ESP_OK && bme_data.valid) {
                
                // Określenie jakości powietrza
                const char* air_quality = get_air_quality_description(bme_data.gas_resistance, bme_data.gas_valid);
                
                // Kompaktowe logowanie z jakością powietrza
                ESP_LOGI(TAG, "🌡️ T:%.1f°C P:%.1fhPa H:%.1f%% G:%.0fΩ [%s]", 
                         bme_data.temperature, 
                         bme_data.pressure, 
                         bme_data.humidity, 
                         bme_data.gas_resistance,
                         air_quality);
                
                // Odczyt danych baterii
                float battery_voltage = battery_monitor_get_voltage();
                uint8_t battery_percentage = battery_monitor_get_percentage();
                bool battery_low = battery_monitor_is_low_battery();
                
                ESP_LOGI(TAG, "🔋 Bateria: %.2fV (%d%%) %s", 
                         battery_voltage, 
                         battery_percentage,
                         battery_low ? "[NISKI POZIOM!]" : "");
                
                // Wysyłanie do MQTT - oddzielne topiki dla każdego parametru
                char temp_str[16], press_str[16], hum_str[16], gas_str[16], gas_valid_str[8];
                char battery_voltage_str[16], battery_percentage_str[8];
                
                snprintf(temp_str, sizeof(temp_str), "%.2f", bme_data.temperature);
                snprintf(press_str, sizeof(press_str), "%.2f", bme_data.pressure);
                snprintf(hum_str, sizeof(hum_str), "%.2f", bme_data.humidity);
                snprintf(gas_str, sizeof(gas_str), "%.0f", bme_data.gas_resistance);
                snprintf(gas_valid_str, sizeof(gas_valid_str), "%d", bme_data.gas_valid);
                snprintf(battery_voltage_str, sizeof(battery_voltage_str), "%.2f", battery_voltage);
                snprintf(battery_percentage_str, sizeof(battery_percentage_str), "%d", battery_percentage);
                
                // Publikowanie do oddzielnych topików
                bool temp_ok = communication_publish_data("bme680/temperature", temp_str);
                bool press_ok = communication_publish_data("bme680/pressure", press_str);
                bool hum_ok = communication_publish_data("bme680/humidity", hum_str);
                bool gas_ok = communication_publish_data("bme680/gas_resistance", gas_str);
                communication_publish_data("bme680/gas_valid", gas_valid_str);
                communication_publish_data("bme680/air_quality", air_quality);
                
                // Publikowanie danych baterii
                bool bat_volt_ok = communication_publish_data("esp32c3/battery_voltage", battery_voltage_str);
                bool bat_perc_ok = communication_publish_data("esp32c3/battery_percentage", battery_percentage_str);
                if (battery_low) {
                    communication_publish_data("esp32c3/battery_alert", "NISKI_POZIOM");
                }
                
                // Dodatkowe informacje systemowe
                char mem_str[16];
                snprintf(mem_str, sizeof(mem_str), "%lu", (unsigned long)esp_get_free_heap_size());
                communication_publish_data("bme680/free_memory", mem_str);
                
                // Informacja o następnym deep sleep
                char sleep_info[32];
                snprintf(sleep_info, sizeof(sleep_info), "SLEEP_%dMIN", SLEEP_TIME_MINUTES);
                communication_publish_data("esp32c3/next_action", sleep_info);
                
                if (temp_ok && press_ok && hum_ok && gas_ok && bat_volt_ok && bat_perc_ok) {
                    ESP_LOGI(TAG, "✅ Wszystkie dane (BME680 + bateria) wysłane pomyślnie");
                    data_sent_successfully = true;
                } else {
                    ESP_LOGE(TAG, "❌ Błąd wysyłania niektórych danych");
                }
            } else {
                ESP_LOGE(TAG, "❌ Błąd odczytu BME680");
            }
            
            // Okno debugowania - czas na podłączenie monitora
            if (data_sent_successfully) {
                ESP_LOGI(TAG, "🛠️ Okno debugowania: %d sekund (czas na podłączenie monitora)", DEBUG_DELAY_SECONDS);
                for (int countdown = DEBUG_DELAY_SECONDS; countdown > 0; countdown--) {
                    ESP_LOGI(TAG, "⏰ Deep sleep za %d sekund...", countdown);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            }
            
        } else {
            ESP_LOGE(TAG, "Nie udało się połączyć z MQTT!");
        }
        
    } else {
        ESP_LOGE(TAG, "Nie udało się połączyć z WiFi!");
    }
    
    // Cleanup
    communication_cleanup();
    bme680_cleanup();
    battery_monitor_cleanup();
    
    // Konfiguracja i wejście w deep sleep
    ESP_LOGI(TAG, "💤 Konfiguracja deep sleep na %d minut...", SLEEP_TIME_MINUTES);
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);
    
    ESP_LOGI(TAG, "💤 Wchodzę w deep sleep... Do zobaczenia za %d minut! 😴", SLEEP_TIME_MINUTES);
    esp_deep_sleep_start();
}
