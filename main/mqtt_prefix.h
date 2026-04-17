#ifndef MQTT_PREFIX_H
#define MQTT_PREFIX_H

/**
 * @brief Zwraca pełny temat MQTT z prefiksem email użytkownika.
 *        Wynik w buforze statycznym — użyj od razu, nie przechowuj wskaźnika.
 *        Przykład: mqtt_topic("gas/total") → "jan_at_wp.pl/gas/total"
 */
const char *mqtt_topic(const char *subtopic);

/**
 * @brief Wczytuje email (prefiks topiku) z NVS namespace "wifi_config", klucz "mqtt_email".
 *        Wywołaj raz przed użyciem mqtt_topic().
 */
void mqtt_prefix_load_from_nvs(void);

#endif // MQTT_PREFIX_H
