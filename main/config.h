#ifndef CONFIG_H
#define CONFIG_H

#define MQTT_TOPIC_CURRENT_SET_TOTAL         "current_consumption/set_total"
#define MQTT_TOPIC_CURRENT_TOTAL             "current_consumption/total"
#define MQTT_TOPIC_CURRENT_24H               "current_consumption/24h"
#define MQTT_TOPIC_CURRENT_24H_PRICE         "current_consumption/24h_price"
#define MQTT_TOPIC_CURRENT_GET_TOTAL         "current_consumption/get_total"
#define MQTT_TOPIC_CURRENT_GET_CURRENT       "current_consumption/get_current"
#define MQTT_TOPIC_CURRENT_SET_PRICE_ONE_KWH "current_consumption/set_price_one_kwh"

// Klucze NVS — max 15 znaków!
#define FLASH_TOTAL_CONSUMPTION "kwh_total"    // 9 znaków
#define FLASH_PRICE_ONE_KWH     "kwh_price"    // 9 znaków
#define FLASH_REPORT_TIME       "report_time"  // 11 znaków


#define ENABLE_LOG_FOR_TIME 1
#define ENABLE_LOG_FOR_TIME_CONTROL 1
#define ENABLE_LOG_FOR_FLASH_MANAGER 1
#define ENABLE_LOG_FOR_CURRENT_SENSOR 1
#define ENABLE_LOG_FOR_COMMUNICATION 1


//  #define LOG_LOCAL_LEVEL ESP_LOG_NONE

// #define PAULINA
 #define BOBIK 1
//#define WESOLA 1

#endif // CONFIG_H
