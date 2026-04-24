#ifndef CONFIG_H
#define CONFIG_H

// === Typ czujnika — wybierz JEDEN ===
// Wpływa na segment topiku MQTT: <email>/<sensor_type>/<dane>
// Przykład: jan_at_gmail.com/gas/total
//           jan_at_gmail.com/electricity/total
//           jan_at_gmail.com/water_temp/current
#define SENSOR_TYPE_WATER_TEMP   "water_temp"
#define SENSOR_TYPE_ELECTRICITY  "electricity"
#define SENSOR_TYPE_GAS          "gas"

// Aktywny typ czujnika dla licznika gazu
#define ACTIVE_SENSOR_TYPE  SENSOR_TYPE_GAS

// === Tematy MQTT — licznik gazu ===
// Tematy są dynamiczne: <email>/<sensor_type>/<subtopic>
// Podtematy (bez prefiksu email — dołączany w runtime przez mqtt_topic())
#define MQTT_SUBTOPIC_GAS_TOTAL_M3           ACTIVE_SENSOR_TYPE "/total_m3"
#define MQTT_SUBTOPIC_GAS_DAILY_M3           ACTIVE_SENSOR_TYPE "/daily_m3"
#define MQTT_SUBTOPIC_GAS_PULSE_COUNT        ACTIVE_SENSOR_TYPE "/pulse_count"
#define MQTT_SUBTOPIC_GAS_SET_TOTAL          ACTIVE_SENSOR_TYPE "/set_total"
#define MQTT_SUBTOPIC_GAS_GET_STATUS         ACTIVE_SENSOR_TYPE "/get_status"

// Klucz NVS dla emaila (prefiksu topiku)
#define FLASH_MQTT_EMAIL_PREFIX  "mqtt_email"   // max 15 znaków klucza NVS

// Makra do budowania pełnego topiku: email + "/" + subtopic
// Używaj funkcji mqtt_topic() z mqtt_prefix.h
#define MQTT_TOPIC_GAS_TOTAL_M3           mqtt_topic(MQTT_SUBTOPIC_GAS_TOTAL_M3)
#define MQTT_TOPIC_GAS_DAILY_M3           mqtt_topic(MQTT_SUBTOPIC_GAS_DAILY_M3)
#define MQTT_TOPIC_GAS_PULSE_COUNT        mqtt_topic(MQTT_SUBTOPIC_GAS_PULSE_COUNT)
#define MQTT_TOPIC_GAS_SET_TOTAL          mqtt_topic(MQTT_SUBTOPIC_GAS_SET_TOTAL)
#define MQTT_TOPIC_GAS_GET_STATUS         mqtt_topic(MQTT_SUBTOPIC_GAS_GET_STATUS)

// Pin przycisku konfiguracji (taki sam jak w temp_wody)
#define BOOT_BUTTON_GPIO 8  // D8 na XIAO ESP32-C3


// #define PAULINA
 #define BOBIK 1
//#define WESOLA 1


#endif // CONFIG_H
