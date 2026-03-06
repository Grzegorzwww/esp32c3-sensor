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



void app_main(void)
{


    ESP_ERROR_CHECK(flash_manager_init());


    if(establish_communication() == ESP_OK){
        sync_time_from_ntp();
        init_time();
    }else{

    }

    communication_enable_advanced_power_save(false); 


    init_current_sensor();

    current_time time_current = {0, 0, 0};

    while(1)
    {

     
        if (get_current_time(&time_current) == ESP_OK) {
           // ESP_LOGI(TAG, "✅ Current time: %02d:%02d:%02d", time_current.hours, time_current.minutes, time_current.seconds);
        }

        current_sensor_analyze_data(&is_6AM_now, &is_one_hour_elapsed);
        // write_uart("hello world");


        
        read_data_from_IEC1107();

        vTaskDelay(pdMS_TO_TICKS(10000));

        }
    }


