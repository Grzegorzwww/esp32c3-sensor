idf_component_register(SRCS "espnow_bridge.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_event esp_netif freertos ap_webserver)
