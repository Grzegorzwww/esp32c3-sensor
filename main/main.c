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
#include "config.h"  // Konfiguracja użytkownika
#include "time_control.h"
#include "current_sensor.h"
#include "flash_manager.h"

// Tag dla logów


static const char *TAG = "MAIN";

esp_err_t establish_communication();

void app_main(void)
{


    ESP_ERROR_CHECK(flash_manager_init());


    if(establish_communication() == ESP_OK){
        init_time();
        sync_time_from_ntp();
    }

    communication_enable_advanced_power_save(false); 


    init_current_sensor();

    current_time time_current = {0, 0, 0};

    while(1)
    {

     
        if (get_current_time(&time_current) == ESP_OK) {
            ESP_LOGI(TAG, "✅ Current time: %02d:%02d:%02d", time_current.hours, time_current.minutes, time_current.seconds);
        }

        current_sensor_analyze_data(&is_5AM_now);


        vTaskDelay(pdMS_TO_TICKS(1000));

        }

}


esp_err_t establish_communication(){
        // Inicjalizacja modułu komunikacji
        esp_err_t ret = communication_init(&parse_mqtt_message);
        if (ret == ESP_OK) {
            // Łączenie z WiFi
            bool wifi_connected = communication_connect_wifi();
            if (wifi_connected) {
               
                bool mqtt_connected = communication_connect_mqtt();
                if (mqtt_connected) {
  
                    // Krótka zwłoka na dokończenie transmisji
                    vTaskDelay(pdMS_TO_TICKS(800));
                    return ESP_OK;
                }
            }
            else{
                return ESP_FAIL;
            }
            //communication_cleanup();
        }
        else{
            return ESP_FAIL;
        }
        return ESP_FAIL;
}