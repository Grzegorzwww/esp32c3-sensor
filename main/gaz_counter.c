#include "gaz_counter.h"

static const char *TAG = "GAZ_COUNTER";

// impulse_count ładowany z NVS po każdym wybudzeniu — nie musi być w RTC
static uint32_t impulse_count = 0;
static float    total_gas     = 0.0f;

// Zmienne w RTC RAM — przeżywają deep sleep
static RTC_DATA_ATTR bool input_stuck = false;  // Flaga permanentnego zwarcia

// KLUCZOWE: stan maszyny musi przeżyć deep sleep!
static RTC_DATA_ATTR wakeup_state_mechine_t wakeup_state_machine = STATE_IDLE;

// ============================================================
//  Maszyna stanów — debouncing kontaktronu przez deep sleep
// ============================================================
//
//  Schemat:
//
//   STATE_IDLE
//     Pin rozwarty, śpimy w GPIO wakeup.
//     Wybudzenie przez GPIO (pin LOW) → STATE_FIRST_CONTACT, sleep timer 500ms
//
//   STATE_FIRST_CONTACT
//     Czekamy 500ms (timer sleep) i sprawdzamy czy pin NADAL zwarty.
//     Tak  → zlicz impuls → STATE_COUNTED, sleep timer 500ms
//     Nie  → fałszywy impuls (szum) → STATE_IDLE, sleep GPIO wakeup
//
//   STATE_COUNTED
//     Impuls zliczony, czekamy aż pin się rozewrze.
//     Pin wciąż zwarty → stuck? (check: czy minęło > INPUT_PIN_STUCK_TIME_S) → STATE_IDLE
//     Pin rozwarty → STATE_IDLE, sleep GPIO wakeup
//
// ============================================================

bool control_wake_up_routine()
{
    bool impulse_counted = false;

    switch (wakeup_state_machine)
    {
    // ----------------------------------------------------------
    case STATE_IDLE:
        // Wybudzeni przez GPIO (pin LOW) — pierwszy kontakt
        if (check_input_is_active()) {
            ESP_LOGI(TAG, "🔍 Stan: IDLE→FIRST_CONTACT (pin zwarty, debounce 500ms)");
            wakeup_state_machine = STATE_FIRST_CONTACT;
        } else {
            // Wybudzenie z innego powodu (reset?) — zostajemy w IDLE
            ESP_LOGI(TAG, "ℹ️  Stan: IDLE — pin rozwarty, powrót do GPIO wakeup");
        }
        break;

    // ----------------------------------------------------------
    case STATE_FIRST_CONTACT:
        // Minęło 500ms — czy pin NADAL zwarty? (debounce)
        if (check_input_is_active()) {
            // Prawdziwy impuls!
            impulse_count++;
            save_gas_to_nvs();
            total_gas = impulse_count * GAS_IMPULSE_VOLUME;
            impulse_counted = true;

            if (impulse_count % IMPULSES_PER_TENTH_M3 == 0) {
                ESP_LOGI(TAG, "⛽ Impuls: %lu (%.3f m³) — PRÓG 0.1 m³!",
                         (unsigned long)impulse_count, total_gas);
            } else {
                ESP_LOGI(TAG, "⛽ Impuls: %lu (%.3f m³)",
                         (unsigned long)impulse_count, total_gas);
            }
            wakeup_state_machine = STATE_COUNTED;
        } else {
            // Szum / fałszywy impuls — ignorujemy
            ESP_LOGW(TAG, "⚠️  Stan: FIRST_CONTACT — pin rozwarty, fałszywy impuls, ignoruję");
            wakeup_state_machine = STATE_IDLE;
        }
        break;

    // ----------------------------------------------------------
    case STATE_COUNTED:
        // Czekamy aż pin się rozewrze po zliczonym impulsie
        if (check_input_is_active()) {
            // Pin wciąż zwarty — może stuck
            if (input_stuck) {
                ESP_LOGW(TAG, "🔒 Stan: COUNTED — pin NADAL zwarty (stuck)");
            } else {
                ESP_LOGI(TAG, "⏳ Stan: COUNTED — pin wciąż zwarty, czekam...");
            }
        } else {
            // Pin się rozwarł — gotowi na następny impuls
            ESP_LOGI(TAG, "✅ Stan: COUNTED→IDLE (pin rozwarty)");
            input_stuck = false;
            wakeup_state_machine = STATE_IDLE;
        }
        break;

    // ----------------------------------------------------------
    default:
        wakeup_state_machine = STATE_IDLE;
        break;
    }

    return impulse_counted;
}

void init_gaz_counter()
{
    ESP_LOGI(TAG, "🔧 Init GPIO%u (kontaktron gazu)", (unsigned)GAS_GPIO_PIN);
    gpio_config_t config = {
        .pin_bit_mask  = (1ULL << GAS_GPIO_PIN),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE
    };
    gpio_config(&config);

    load_gas_from_nvs();
    total_gas = impulse_count * GAS_IMPULSE_VOLUME;

    ESP_LOGI(TAG, "📊 Stan: %lu impulsów (%.3f m³) | maszyna: %d",
             (unsigned long)impulse_count, total_gas, wakeup_state_machine);
}

bool check_input_is_active()
{
    return (gpio_get_level(GAS_GPIO_PIN) == 0); // LOW = zwarty
}

float get_total_gas()
{
    return total_gas;
}

uint32_t get_impulse_count()
{
    return impulse_count;
}

void set_total_gas(float value_m3)
{
    impulse_count = (uint32_t)(value_m3 / GAS_IMPULSE_VOLUME + 0.5f);
    total_gas = impulse_count * GAS_IMPULSE_VOLUME;
    save_gas_to_nvs();
    ESP_LOGI(TAG, "⚙️ Stan licznika ustawiony: %.3f m³ (%lu impulsów)",
             total_gas, (unsigned long)impulse_count);
}

bool is_one_m3_completed()
{
    return (impulse_count > 0 && impulse_count % IMPULSES_PER_M3 == 0);
}

bool is_one_tenth_m3_completed()
{
    return (impulse_count > 0 && impulse_count % IMPULSES_PER_TENTH_M3 == 0);
}

// ============================================================
//  Zarządzanie deep sleep
// ============================================================

void go_to_cpu_sleep()
{
    // Jeśli pin jest zwarty → może być stuck — śpij na timer
    if (check_input_is_active()) {
        ESP_LOGW(TAG, "⚠️  Pin zwarty przed snem — tryb timer %ds (stuck guard)",
                 INPUT_PIN_STUCK_TIME_S);
        input_stuck = true;
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(
            (uint64_t)INPUT_PIN_STUCK_TIME_S * 1000000ULL));
    } else {
        // Normalny tryb — wybudzenie przez GPIO (pin LOW)
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        uint64_t mask = (1ULL << GAS_GPIO_PIN);
        ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW));
        ESP_LOGI(TAG, "💤 Deep sleep — GPIO%d wakeup (czekam na impuls)",
                 (int)GAS_GPIO_PIN);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

void go_to_cpu_sleep_for_ms(uint32_t ms)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL));
    ESP_LOGI(TAG, "💤 Deep sleep — timer %u ms (debounce)", ms);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

bool is_input_stuck(void)   { return input_stuck; }
void set_input_stuck(bool s) { input_stuck = s; }

// ============================================================
//  NVS
// ============================================================

void save_gas_to_nvs()
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, NVS_KEY, impulse_count);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

void load_gas_from_nvs()
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        uint32_t val = 0;
        if (nvs_get_u32(nvs, NVS_KEY, &val) == ESP_OK) {
            impulse_count = val;
        }
        nvs_close(nvs);
    }
}



