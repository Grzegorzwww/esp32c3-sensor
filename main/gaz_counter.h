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
#define GAS_GPIO_PIN GPIO_NUM_5    // GPIO dla kontaktronu
#define GAS_IMPULSE_VOLUME 0.001f  // m³ na impuls  (cyferblat: 00000,001)
#define IMPULSES_PER_M3 1000       // Impulsów na 1 m³
#define IMPULSES_PER_TENTH_M3 100  // Impulsów na 0.1 m³ → wysyłka MQTT co 00000,100
#define NVS_NAMESPACE "gas"
#define NVS_KEY "total"

#define DELAY_AFTER_WAKEUP_MS 2000  // Opóźnienie po wybudzeniu (ms)
#define INPUT_PIN_STUCK_TIME_S 10
#define SLEEP_PERIOD_MS 500
#define LOG_INTERVAL_HOURS   24
#define WAKEUPS_PER_LOG  ((LOG_INTERVAL_HOURS * 3600UL * 1000UL) / SLEEP_PERIOD_MS)

typedef enum {
    INPUT_WAS_ACTIVE_WAKE_UP,
    INPUT_WAS_DEACTIVE_WAKE_UP
} wakeup_state_mechine_t;

// Konfiguracja licznika gazu
#define GAS_GPIO_PIN GPIO_NUM_5 // GPIO5

void go_to_cpu_sleep_for_ms( uint32_t ms);
bool control_wake_up_routine();

void go_to_cpu_sleep();
bool check_input_is_active();

void init_gaz_counter();
bool check_wake_up_reason();
float get_total_gas();
bool is_one_m3_completed();
bool is_one_tenth_m3_completed();

bool is_input_stuck(void);
void set_input_stuck(bool stuck);

void save_gas_to_nvs();
void load_gas_from_nvs();