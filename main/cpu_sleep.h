#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"



// Konfiguracja licznika gazu
#define GAS_GPIO_PIN GPIO_NUM_5 // ZMIANA: używamy teraz GPIO5 (RTC, obsługuje wakeup na ESP32-C3)

void init_cpu_sleep();
void go_to_cpu_sleep();
bool check_input_is_high();