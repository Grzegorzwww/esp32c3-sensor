#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdbool.h>
#include "esp_err.h"
#include "config.h"  // Konfiguracja użytkownika
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"


typedef enum {
    WIFI_IN_NORMAL_MODE,
    WIFI_IN_AP_MODE,
    WIFI_NOT_CONFIGURED
} wifi_connection_mode_t;

#define BOOT_BUTTON_GPIO 8

typedef bool (* parse_mqtt_data_callback_t)(const char* topic, const char* data);

// bool current_sensor_analyze_data(bool (*is_time)(void));
/**
 * @brief Inicjalizuje moduł komunikacji (WiFi)
 * @return ESP_OK jeśli sukces
 */
esp_err_t communication_init(parse_mqtt_data_callback_t incoming_mqtt_data_callback);

/**
 * @brief Łączy się z WiFi
 * @return true jeśli połączenie udane
 */
bool communication_connect_wifi(void);

/**
 * @brief Łączy się z MQTT broker
 * @return true jeśli połączenie udane
 */
bool communication_connect_mqtt(void);

/**
 * @brief Włącza zaawansowany Power Management (DFS + Light Sleep)
 * UWAGA: To jest opcjonalne - Modem Sleep jest już włączony automatycznie
 * @param enable_light_sleep Czy włączyć automatyczny Light Sleep
 * @return ESP_OK jeśli sukces
 */
esp_err_t communication_enable_advanced_power_save(bool enable_light_sleep);

/**
 * @brief Publikuje dane przez MQTT
 * @param topic Topic MQTT
 * @param data Dane do wysłania
 * @return true jeśli publikacja udana
 */
bool communication_publish_data(const char* topic, const char* data);

/**
 * @brief Sprawdza czy MQTT jest połączony
 * @return true jeśli połączony
 */
bool communication_is_mqtt_connected(void);

/**
 * @brief Resetuje flagę wysyłania informacji o połączeniu
 * Używaj gdy chcesz ponownie wysłać info o połączeniu
 */
void communication_reset_connection_info_flag(void);

/**
 * @brief Czyści zasoby komunikacji
 */
void communication_cleanup(void);


bool sync_time_from_ntp(void);

esp_err_t establish_communication();

esp_err_t  communication_create_wifi_ap();

esp_err_t create_configuration_html_page();

#endif // COMMUNICATION_H