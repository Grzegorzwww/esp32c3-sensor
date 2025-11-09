#include "cpu_sleep.h"
#include "esp_sleep.h"
#include "gaz_counter.h"
#include "esp_chip_info.h"
#include "driver/rtc_io.h"

static const char *TAG = "CPU_SLEEP";

void init_cpu_sleep()
{
    ESP_LOGI(TAG, "Init sleep GPIO%u", (unsigned)GAS_GPIO_PIN);
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << GAS_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    }; 
    gpio_config(&config);
}

bool check_input_is_high()
{
    int level = gpio_get_level(GAS_GPIO_PIN);
    ESP_LOGI(TAG, "🔍 Sprawdzanie stanu GPIO%u: %d", (unsigned)GAS_GPIO_PIN, level);
    return (level == 1);
}

static void log_rtc_levels()
{ 
    for(int g=0; g<=5; ++g) 
        ESP_LOGI(TAG, "RTC GPIO%d level=%d", g, gpio_get_level(g)); 
}

void go_to_cpu_sleep()
{
    esp_chip_info_t chip; 
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Chip model=%d", chip.model);
    log_rtc_levels();
    
    int lvl = gpio_get_level(GAS_GPIO_PIN);
    ESP_LOGI(TAG, "Przed snem GPIO%u=%d", (unsigned)GAS_GPIO_PIN, lvl);

    // Sprawdź czy pin jest zwarty PRZED snem
    if (lvl == 0) {
        // Pin zwarty - ustaw flagę i idź spać na 5 sekund (timer)
        ESP_LOGW(TAG, "Pin zwarty przed snem - aktywuję tryb stuck (timer 5s)");
        set_input_stuck(true);
        
        // WYŁĄCZ GPIO wakeup (ważne!)
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        
        // Włącz TYLKO timer
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(10 * 1000000ULL)); // 5 sekund
        ESP_LOGI(TAG, "Deep sleep (timer 5s) - GPIO wake WYŁĄCZONY");
    } else {
        // Pin rozwarty - normalny tryb GPIO wakeup
        ESP_LOGI(TAG, "Pin HIGH - normalny tryb GPIO wakeup");
        
        // WYŁĄCZ timer (jeśli był włączony)
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        
        // Włącz TYLKO GPIO wakeup
        uint64_t mask = BIT(GAS_GPIO_PIN);
        ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW));
        ESP_LOGI(TAG, "💤 Deep sleep (GPIO wake). Timer WYŁĄCZONY. Zwarcie GPIO%u->GND wybudzi.", (unsigned)GAS_GPIO_PIN);
    }

    vTaskDelay(pdMS_TO_TICKS(150));
    esp_deep_sleep_start();
}