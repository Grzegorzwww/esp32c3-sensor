#include "gaz_counter.h"

static const char *TAG = "GAZ_COUNTER";

static uint32_t impulse_count = 0;
static float total_gas = 0.0f;

static RTC_DATA_ATTR bool input_stuck = false; // Flaga permanentnego zwarcia

void init_gaz_counter()
{

    ESP_LOGI(TAG, "Init sleep GPIO%u", (unsigned)GAS_GPIO_PIN);
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << GAS_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&config);

    load_gas_from_nvs();
    total_gas = impulse_count * GAS_IMPULSE_VOLUME;

    ESP_LOGI(TAG, "Stan: %u impulsów (%.3f m³)",
             impulse_count, total_gas);
}

bool check_input_is_active()
{
    int level = gpio_get_level(GAS_GPIO_PIN);
    return (level == 0); // low = active
}

float get_total_gas()
{
    return total_gas;
}

bool is_one_m3_completed()
{
    return (impulse_count % IMPULSES_PER_M3 == 0);
}

bool is_one_tenth_m3_completed()
{
    return (impulse_count % IMPULSES_PER_TENTH_M3 == 0);
}

void go_to_cpu_sleep()
{

    bool input_is_active = check_input_is_active();

    if (input_is_active)
    {

        ESP_LOGW(TAG, "Pin zwarty aktywuję tryb stuck (timer 5s)");
        set_input_stuck(true);

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO); // WYŁĄCZ GPIO wakeup

        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(INPUT_PIN_STUCK_TIME_S * 1000000ULL)); // WYŁĄCZ GPIO wakeup-  10 sekund

        ESP_LOGI(TAG, "Deep sleep (timer 10s) - ON,  (GPIO wakeup) - OFF");
    }
    else
    {

        ESP_LOGI(TAG, "input_is_active = OFF  normalny tryb GPIO wakeup");

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER); // WYŁĄCZ timer

        uint64_t mask = BIT(GAS_GPIO_PIN);
        ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW)); // Włącz TYLKO GPIO wakeup

        ESP_LOGI(TAG, "Deep sleep (GPIO wake) ON, TIMER = OFF () Zwarcie GPIO->GND wybudzi)");
    }

    vTaskDelay(pdMS_TO_TICKS(150));
    esp_deep_sleep_start();
}

bool check_wake_up_reason()
{

    // Sprawdź przyczynę wybudzenia
    bool is_wakeup_by_gas = false;

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    ESP_LOGI(TAG, "Wake cause raw=%d, input_stuck=%d", wakeup_reason, input_stuck);

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_GPIO: 
        if (input_stuck)
        {
            bool actual_input_state = check_input_is_active();  // Sprawdź czy pin się rozwarł

            if (!actual_input_state)
            {
                input_stuck = false;
                ESP_LOGI(TAG, " Pin rozwarty, powrót do normalnego trybu");
            }
            else
            {
                ESP_LOGW(TAG, " Pin zwart, pozostaję w trybie stuck");
            }
            return false; // Nie liczy impulsu
        }

        // Normalny impuls gazu
        is_wakeup_by_gas = true;
        impulse_count++;
        save_gas_to_nvs();
        total_gas = impulse_count * GAS_IMPULSE_VOLUME;
        ESP_LOGI(TAG, "Impuls: %u (%.3f m³)", impulse_count, total_gas);
        break;

    case ESP_SLEEP_WAKEUP_TIMER:

        bool actual_input_state = check_input_is_active();
        if (!actual_input_state)
        {
            input_stuck = false;
        }
        else
        {
            // Nadal zwarty
        }
        break;

    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        ESP_LOGI(TAG, "Pierwsze uruchomienie / reset");
        input_stuck = false;
        break;
    }

    return is_wakeup_by_gas;
}

bool is_input_stuck(void)
{
    return input_stuck;
}

void set_input_stuck(bool stuck)
{
    input_stuck = stuck;
}

void save_gas_to_nvs()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        nvs_set_u32(nvs, NVS_KEY, impulse_count);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

void load_gas_from_nvs()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        uint32_t val = 0;
        if (nvs_get_u32(nvs, NVS_KEY, &val) == ESP_OK)
        {
            impulse_count = val;
        }
        nvs_close(nvs);
    }
}