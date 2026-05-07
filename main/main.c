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
#include "a3144.h"

// Tag dla logów
static const char *TAG = "MAIN";

void establish_communication();

void app_main(void)
{

    ESP_ERROR_CHECK(nvs_flash_init());

    mqtt_prefix_load_from_nvs();

    init_gaz_counter();  // inicjalizuje też A3144
    a3144_led_init(A3144_LED_GPIO);
    battery_monitor_init();

    // vTaskDelay(pdMS_TO_TICKS(500)); // Czekaj na USB CDC — bez tego pierwsze logi giną

    ESP_LOGI(TAG, "START | Impulsów: %lu | Gaz: %.3f m³",
              (unsigned long)get_impulse_count(), get_total_gas());



    communication_check_and_handle_config_button();

    if (control_wake_up_routine()) {
        if (is_one_tenth_m3_completed()) {
            establish_communication();
        }
    }
    go_to_cpu_sleep_for_ms(SLEEP_PERIOD_MS);

    // while (1) {
    //     vTaskDelay(pdMS_TO_TICKS(200));
    //     bool magnet = a3144_is_magnet_detected(A3144_DEFAULT_GPIO);
    //     if (magnet) {
    //         ESP_LOGI(TAG, "🧲 MAGNET DETECTED (GPIO%d = LOW)", A3144_DEFAULT_GPIO);
    //     }
    // }

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

                // Bateria
                battery_data_t bat;
                if (battery_monitor_read(&bat) == ESP_OK && bat.valid) {
                    snprintf(data, sizeof(data), "%.2f", bat.voltage);
                    communication_publish_data(mqtt_topic("battery/voltage"), data);
                    snprintf(data, sizeof(data), "%d", bat.percentage);
                    communication_publish_data(mqtt_topic("battery/percent"), data);
                    ESP_LOGI(TAG, "MQTT battery: %.2fV (%d%%)", bat.voltage, bat.percentage);
                }

            
                 publish_timestamp();

                 publish_wifi_quality();

                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        communication_cleanup();
    }
}