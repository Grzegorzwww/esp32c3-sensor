#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

/**
 * @brief Biblioteka obsługi czujnika Halla A3144
 *
 * A3144 to czujnik Halla z wyjściem open-drain (aktywnie niski).
 * Wyjście LOW = magnes w pobliżu (impuls wykryty)
 * Wyjście HIGH = brak magnesu
 *
 * Typowe podłączenie:
 *   VCC → 3.3V
 *   GND → GND
 *   OUT → GPIO (z pull-up wewnętrznym lub zewnętrznym ~10kΩ)
 */

// Domyślny pin GPIO dla A3144 (D7 = GPIO20 na XIAO ESP32-C3)
#define A3144_DEFAULT_GPIO  GPIO_NUM_20

/** Stan wyjścia czujnika */
typedef enum {
    A3144_NO_MAGNET = 0,   ///< Brak magnesu (wyjście HIGH)
    A3144_MAGNET_DETECTED, ///< Magnes w pobliżu (wyjście LOW)
} a3144_state_t;

// Pin LED debug (GPIO21 = D6 na XIAO ESP32-C3)
#define A3144_LED_GPIO  GPIO_NUM_21

/**
 * @brief Inicjalizacja pinu GPIO jako wyjście dla LED debug
 */
void a3144_led_init(gpio_num_t led_pin);

/**
 * @brief Zapal LED (magnes wykryty)
 */
void a3144_led_set(gpio_num_t led_pin);

/**
 * @brief Zgaś LED (brak magnesu)
 */
void a3144_led_clear(gpio_num_t led_pin);

/**
 * @brief Inicjalizacja pinu GPIO czujnika A3144
 *
 * Konfiguruje pin jako wejście z wewnętrznym pull-up.
 *
 * @param gpio_pin  Numer pinu GPIO
 */
void a3144_init(gpio_num_t gpio_pin);

/**
 * @brief Odczyt aktualnego stanu czujnika (polling)
 *
 * @param gpio_pin  Numer pinu GPIO (ten sam co w a3144_init)
 * @return A3144_MAGNET_DETECTED jeśli magnes w pobliżu, A3144_NO_MAGNET jeśli nie
 */
a3144_state_t a3144_read(gpio_num_t gpio_pin);

/**
 * @brief Czy czujnik aktualnie wykrywa magnes?
 *
 * Wygodny wrapper zwracający bool.
 *
 * @param gpio_pin  Numer pinu GPIO
 * @return true jeśli magnes wykryty
 */
bool a3144_is_magnet_detected(gpio_num_t gpio_pin);
