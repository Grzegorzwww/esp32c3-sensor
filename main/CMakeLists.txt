idf_component_register(SRCS "main.c" "communication.c" "bme680_wrapper.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "certificates/server_cert.pem"
                    REQUIRES mqtt esp_wifi esp_netif esp_event nvs_flash esp-tls mbedtls driver esp8266_wrapper bme680)
