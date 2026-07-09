idf_component_register(SRCS "ap_webserver.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_netif esp_event nvs_flash driver esp_http_server esp_system esp_timer)
