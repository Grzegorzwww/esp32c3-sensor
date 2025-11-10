#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "communication.h"
#include "battery_monitor.h"
#include "nvs.h"
#include "gaz_counter.h"
#include "config.h"  // Konfiguracja użytkownika

// Tag dla logów


static const char *TAG = "MAIN";

void establish_communication();

void app_main(void)
{
    // Inicjalizacja podstawowych systemów
   
    ESP_ERROR_CHECK(nvs_flash_init());

    init_gaz_counter();


    // ESP_LOGI(TAG, "WYBUDZONY !");
    // vTaskDelay(pdMS_TO_TICKS(1000));


    bool is_wakeup_by_gas = check_wake_up_reason();
    ESP_LOGI(TAG, "total gas %f", get_total_gas());
  

    if (is_wakeup_by_gas && is_one_tenth_m3_completed()) {

        establish_communication();
    }

    // vTaskDelay(pdMS_TO_TICKS(DELAY_AFTER_WAKEUP_MS));
    // vTaskDelay(pdMS_TO_TICKS(1000));
     

    go_to_cpu_sleep();

}


void establish_communication(){
        // Inicjalizacja modułu komunikacji
        esp_err_t ret = communication_init();
        if (ret == ESP_OK) {
            // Łączenie z WiFi
            bool wifi_connected = communication_connect_wifi();
            if (wifi_connected) {
                // Łączenie z MQTT
                bool mqtt_connected = communication_connect_mqtt();
                if (mqtt_connected) {
                    // Wysyłka danych gazu (retained)
                    char data[32];
                    snprintf(data, sizeof(data), "%.3f", get_total_gas());
                    communication_publish_data("gaz_consumption", data);
                    ESP_LOGI(TAG, "✅ Dane wysłane: %.3f m³", get_total_gas());
                    
                    // Krótka zwłoka na dokończenie transmisji
                    vTaskDelay(pdMS_TO_TICKS(800));
                }
            }
            communication_cleanup();
        }
}