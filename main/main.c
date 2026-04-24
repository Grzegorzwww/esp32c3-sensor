#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mqtt_prefix.h" // Musi być przed config.h — używa mqtt_topic()
#include "config.h"      // Konfiguracja użytkownika
#include "communication.h"
#include "battery_monitor.h"
#include "nvs.h"
#include "gaz_counter.h"

// Tag dla logów
static const char *TAG = "MAIN";

void establish_communication();

void app_main(void)
{

    ESP_ERROR_CHECK(nvs_flash_init());

    mqtt_prefix_load_from_nvs();

    init_gaz_counter();

    // vTaskDelay(pdMS_TO_TICKS(500)); // Czekaj na USB CDC — bez tego pierwsze logi giną

    // ESP_LOGI(TAG, "START | Impulsów: %lu | Gaz: %.3f m³",
    //           (unsigned long)get_impulse_count(), get_total_gas());



    communication_check_and_handle_config_button();

    if(control_wake_up_routine()){

        //  uint32_t total_impulses = get_impulse_count();

        // if(total_impulses > 0 && total_impulses % IMPULSES_PER_TENTH_M3 == 0){
        if( is_one_tenth_m3_completed()){
        // if( total_impulses % 5 == 0){
            establish_communication();
        }
    }

    go_to_cpu_sleep_for_ms(SLEEP_PERIOD_MS);

  
}

void establish_communication()
{
    esp_err_t ret = communication_init();
    if (ret == ESP_OK)
    {
        bool wifi_connected = communication_connect_wifi();
        if (wifi_connected)
        {
            // Synchronizuj czas NTP i zaktualizuj bazę dzienną
            if (sync_time_from_ntp()) {
                update_daily_base();
                ESP_LOGI(TAG, "NTP zsynchronizowany, dzienny=%0.3f m³", get_daily_gas());
            }

            bool mqtt_connected = communication_connect_mqtt();
            if (mqtt_connected)
            {
                char data[32];

                // Łączna wartość licznika
                snprintf(data, sizeof(data), "%.3f", get_total_gas());
                communication_publish_data(mqtt_topic(MQTT_SUBTOPIC_GAS_TOTAL_M3), data);
                ESP_LOGI(TAG, "MQTT total: %s = %s m³", mqtt_topic(MQTT_SUBTOPIC_GAS_TOTAL_M3), data);

                // Dzienne zużycie
                snprintf(data, sizeof(data), "%.3f", get_daily_gas());
                communication_publish_data(mqtt_topic(MQTT_SUBTOPIC_GAS_DAILY_M3), data);
                ESP_LOGI(TAG, "MQTT daily: %s = %s m³", mqtt_topic(MQTT_SUBTOPIC_GAS_DAILY_M3), data);

            
                 publish_timestamp();

                 publish_wifi_quality();

                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        communication_cleanup();
    }
}