#include "a3144.h"

#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "A3144";

// ===================== LED DEBUG =====================

void a3144_led_init(gpio_num_t led_pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << led_pin),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(led_pin, 0); // domyślnie zgaszona
    ESP_LOGI(TAG, "💡 LED debug zainicjalizowany na GPIO%d", led_pin);
}

void a3144_led_set(gpio_num_t led_pin)
{
    gpio_set_level(led_pin, 1);
}

void a3144_led_clear(gpio_num_t led_pin)
{
    gpio_set_level(led_pin, 0);
}

// ===================== CZUJNIK =====================

void a3144_init(gpio_num_t gpio_pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << gpio_pin),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,   // A3144 ma wyjście open-drain — pull-up wymagany
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    

    ESP_LOGI(TAG, "✅ A3144 zainicjalizowany na GPIO%d (pull-up włączony)", gpio_pin);
}

a3144_state_t a3144_read(gpio_num_t gpio_pin)
{
    // A3144: wyjście LOW = magnes wykryty (open-drain aktywnie niski)
    int level = gpio_get_level(gpio_pin);
    ESP_LOGI(TAG, "Odczyt GPIO%d: %d → %s", gpio_pin, level,
             (level == 0) ? "MAGNET_DETECTED" : "NO_MAGNET");
    return (level == 0) ? A3144_MAGNET_DETECTED : A3144_NO_MAGNET;
}

bool a3144_is_magnet_detected(gpio_num_t gpio_pin)
{
    bool detected = a3144_read(gpio_pin) == A3144_MAGNET_DETECTED;
    // Aktualizuj LED debug jeśli zainicjalizowany
    if (detected) {
        a3144_led_set(A3144_LED_GPIO);
    } else {
        a3144_led_clear(A3144_LED_GPIO);
    }
    return detected;
}
