idf_component_register(SRCS "main.c" "communication.c" "battery_monitor.c" "gaz_counter.c" "mqtt_prefix.c" "a3144.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "certificates/server_cert.pem"
                    REQUIRES mqtt esp_wifi esp_netif esp_event nvs_flash esp-tls mbedtls driver esp_adc esp8266_wrapper esp_http_server)
