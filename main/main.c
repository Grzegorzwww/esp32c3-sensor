#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mqtt_prefix.h"  // Musi być przed config.h — używa mqtt_topic()
#include "config.h"       // Konfiguracja użytkownika
#include "communication.h"
#include "battery_monitor.h"
#include "nvs.h"
#include "gaz_counter.h"

// Tag dla logów
static const char *TAG = "MAIN";

// ============================================================
//  Okresowe logowanie stanu co 5 minut
// ============================================================
#define STATUS_LOG_INTERVAL_MS (5 * 60 * 1000)  // 5 minut

static TimerHandle_t status_log_timer = NULL;

void status_log_timer_callback(TimerHandle_t xTimer)
{
    // Budzimy urządzenie i wysyłamy status
    ESP_LOGI(TAG, "⏰ Timer co 5 min — wysyłam status licznika");
    
    // Próba połączenia i wysłania statusu
    esp_err_t ret = communication_init();
    if (ret == ESP_OK)
    {
        bool wifi_connected = communication_connect_wifi();
        if (wifi_connected)
        {
            bool mqtt_connected = communication_connect_mqtt();
            if (mqtt_connected)
            {
                // ========== STATUS LOG (PERIODYCZNY) ==========
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), 
                         "{\"event\":\"periodic_status\",\"impulse_count\":%lu,\"total_m3\":%.3f,\"version\":\"2.0\"}",
                         get_impulse_count(), get_total_gas());
                
                const char *status_topic = mqtt_topic("status");
                communication_publish_data(status_topic, status_msg);
                ESP_LOGI(TAG, "📊 Periodic status: %s → %s", status_topic, status_msg);
                vTaskDelay(pdMS_TO_TICKS(200));
                
                // ========== GŁÓWNE DANE GAZU ==========
                char data[32];
                snprintf(data, sizeof(data), "%.3f", get_total_gas());
                
                const char *topic = mqtt_topic(MQTT_SUBTOPIC_GAS_TOTAL_M3);
                communication_publish_data(topic, data);
                ESP_LOGI(TAG, "✅ Periodic data: %s = %.3f m³", topic, get_total_gas());
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        communication_cleanup();
    }
}

void establish_communication();

void app_main(void)
{
    // Inicjalizacja podstawowych systemów
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Wczytaj email/prefiks MQTT z NVS
    mqtt_prefix_load_from_nvs();
    
    init_gaz_counter();

    // ========== TIMER CO 5 MINUT ==========
    status_log_timer = xTimerCreate("StatusLogTimer", 
                                    pdMS_TO_TICKS(STATUS_LOG_INTERVAL_MS),
                                    pdTRUE,  // auto-reload
                                    NULL,    // pvTimerID
                                    status_log_timer_callback);
    if (status_log_timer != NULL) {
        xTimerStart(status_log_timer, 0);
        ESP_LOGI(TAG, "✅ Timer status log co 5 minut — aktywny");
    } else {
        ESP_LOGW(TAG, "⚠️  Nie mogę utworzyć timera status logu");
    }

    // Sprawdź stan przycisku konfiguracji (GPIO6/D7)
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&button_config);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    int button_state = gpio_get_level(BOOT_BUTTON_GPIO);
    ESP_LOGI(TAG, "🔘 Przycisk konfiguracji (GPIO%d): %s", BOOT_BUTTON_GPIO, 
             button_state ? "ZWOLNIONY" : "NACIŚNIĘTY");
    
    if (button_state == 0) {
        // Przycisk naciśnięty → tryb konfiguracji AP
        ESP_LOGI(TAG, "🔧 Tryb konfiguracji - uruchamiam Access Point");
        communication_create_wifi_ap();
        // W trybie AP nie usypiamy — czekamy na konfigurację
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (control_wake_up_routine())
    {
        establish_communication();
    }
}

void establish_communication()
{
    // Inicjalizacja modułu komunikacji
    esp_err_t ret = communication_init();
    if (ret == ESP_OK)
    {
        // Łączenie z WiFi
        bool wifi_connected = communication_connect_wifi();
        if (wifi_connected)
        {
            // Łączenie z MQTT
            bool mqtt_connected = communication_connect_mqtt();
            if (mqtt_connected)
            {
                // ========== STATUS LOG POŁĄCZENIA (EVENT-BASED) ==========
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), 
                         "{\"event\":\"pulse_detected\",\"impulse_count\":%lu,\"total_m3\":%.3f,\"version\":\"2.0\"}",
                         get_impulse_count(), get_total_gas());
                
                const char *status_topic = mqtt_topic("status");
                communication_publish_data(status_topic, status_msg);
                ESP_LOGI(TAG, "📊 Pulse event: %s → %s", status_topic, status_msg);
                vTaskDelay(pdMS_TO_TICKS(200));
                
                // ========== GŁÓWNE DANE GAZU ==========
                // Wysyłka danych gazu z dynamicznym topikiem (retained)
                char data[32];
                snprintf(data, sizeof(data), "%.3f", get_total_gas());
                
                const char *topic = mqtt_topic(MQTT_SUBTOPIC_GAS_TOTAL_M3);
                communication_publish_data(topic, data);
                ESP_LOGI(TAG, "✅ Pulse data: %s = %.3f m³", topic, get_total_gas());
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        communication_cleanup();
    }
    // ⚠️  Timer będzie nadal działać i wysyłać status co 5 minut
    go_to_cpu_sleep_for_ms(SLEEP_PERIOD_MS);
}