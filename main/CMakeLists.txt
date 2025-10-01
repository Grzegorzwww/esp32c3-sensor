idf_component_register(SRCS "main.c" "communication.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "certificates/server_cert.pem"
                    REQUIRES mqtt esp_wifi esp_netif esp_event nvs_flash esp-tls mbedtls)
