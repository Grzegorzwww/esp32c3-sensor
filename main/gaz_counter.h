#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "nvs.h"

// Konfiguracja licznika gazu
#define GAS_GPIO_PIN GPIO_NUM_5 // GPIO dla kontraktonu
#define GAS_IMPULSE_VOLUME 0.001f // m³ na impuls
#define IMPULSES_PER_M3 1000 // Impulsów na 1 m³
#define IMPULSES_PER_TENTH_M3 100 // Impulsów na 0.1 m³
#define NVS_NAMESPACE "gas"
#define NVS_KEY "total"

void init_gaz_counter();
bool check_wake_up_reason();
float get_total_gas();
bool is_one_m3_completed();
bool is_one_tenth_m3_completed();

bool is_input_stuck(void);
void set_input_stuck(bool stuck);

void save_gas_to_nvs();
void load_gas_from_nvs();