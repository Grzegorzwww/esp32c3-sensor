#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdbool.h>
#include "esp_err.h"
#include "config.h"  // Konfiguracja użytkownika




/**
 * @brief Inicjalizuje moduł komunikacji (WiFi)
 * @return ESP_OK jeśli sukces
 */
esp_err_t communication_init(void);

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



#endif // COMMUNICATION_H