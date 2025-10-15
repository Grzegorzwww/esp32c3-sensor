idf_component_register(SRCS "main.c" "lm35.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash driver esp_adc)
