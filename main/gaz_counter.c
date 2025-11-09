#include "gaz_counter.h"


static const char *TAG = "GAZ_COUNTER";

static uint32_t impulse_count = 0;
static float total_gas = 0.0f;

// Flaga permanentnego zwarcia - przetrwa deep sleep (RTC memory)
static RTC_DATA_ATTR bool input_stuck = false;




void init_gaz_counter()
{

    load_gas_from_nvs();
    total_gas = impulse_count * GAS_IMPULSE_VOLUME;
    
    ESP_LOGI(TAG, "Stan: %u impulsów (%.3f m³)", 
             impulse_count, total_gas);

}


float get_total_gas(){
    return total_gas;
}


bool is_one_m3_completed(){
    return (impulse_count % IMPULSES_PER_M3 == 0);
}

bool is_one_tenth_m3_completed(){
    return (impulse_count % IMPULSES_PER_TENTH_M3 == 0);
}

bool check_wake_up_reason(){

    // Sprawdź przyczynę wybudzenia
    bool is_wakeup_by_gas = false;
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "🔎 Wake cause raw=%d, input_stuck=%d", wakeup_reason, input_stuck);
    
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_GPIO:  // dla ESP32-C3
            // Sprawdź czy to wyjście z trybu "stuck"
            if (input_stuck) {
                // Sprawdź czy pin się rozwarł
                int level = gpio_get_level(GAS_GPIO_PIN);
                ESP_LOGI(TAG, "Sprawdzanie rozwarcia: GPIO%u=%d", (unsigned)GAS_GPIO_PIN, level);
                
                if (level == 1) {
                    // Pin rozwarty - wróć do normalnego trybu
                    input_stuck = false;
                    ESP_LOGI(TAG, " Pin rozwarty - powrót do normalnego trybu");
                } else {
                    // Nadal zwarty - ignoruj, pozostań w trybie stuck
                    ESP_LOGW(TAG, " Pin nadal zwarty - pozostaję w trybie stuck");
                }
                return false; // Nie liczy impulsu
            }
            
            // Normalny impuls - policz go
            is_wakeup_by_gas = true;
            impulse_count++;
            save_gas_to_nvs();
            total_gas = impulse_count * GAS_IMPULSE_VOLUME;
            ESP_LOGI(TAG, "Impuls: %u (%.3f m³)", impulse_count, total_gas);
            break;
            
        case ESP_SLEEP_WAKEUP_TIMER:
            // Wybudzenie z timera (tryb stuck recovery)
            int level = gpio_get_level(GAS_GPIO_PIN);
            if (level == 1) {
                // Pin rozwarty - koniec trybu stuck
                input_stuck = false;
            } else {
                // Nadal zwarty - kontynuuj tryb stuck
            }
            break;
            
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(TAG, "🔄 Pierwsze uruchomienie / reset");
            input_stuck = false; // Reset flagi przy starcie
            break;
    }

    return is_wakeup_by_gas;
}

bool is_input_stuck(void){
    return input_stuck;
}

void set_input_stuck(bool stuck){
    input_stuck = stuck;
}

void save_gas_to_nvs()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_u32(nvs, NVS_KEY, impulse_count);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

void load_gas_from_nvs()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        uint32_t val = 0;
        if (nvs_get_u32(nvs, NVS_KEY, &val) == ESP_OK) {
            impulse_count = val;
        }
        nvs_close(nvs);
    }
}