#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Struktura danych baterii
typedef struct {
    float voltage;          // Napięcie baterii w V
    int percentage;         // Poziom baterii w %
    bool low_battery;       // Czy bateria słaba
    bool valid;             // Czy pomiar jest prawidłowy
} battery_data_t;

// Konfiguracja dzielnika napięcia
// Dla dzielnika R1=10kΩ, R2=10kΩ: Vout = Vin * R2/(R1+R2) = Vin * 0.5
// Maksymalne Vin = 6.6V -> Vout = 3.3V (bezpieczne dla ESP32)
#define BATTERY_R1_OHMS         10000   // Górny rezystor (10kΩ)
#define BATTERY_R2_OHMS         10000   // Dolny rezystor (10kΩ)
#define BATTERY_DIVIDER_RATIO   2.0f    // Stosunek dzielnika (R1+R2)/R2
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_0  // GPIO0 (D0)
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_11 // 0-3.3V
#define BATTERY_MIN_VOLTAGE     3.0f    // Minimalne napięcie (bateria rozładowana)
#define BATTERY_MAX_VOLTAGE     4.2f    // Maksymalne napięcie (bateria naładowana)

/**
 * @brief Inicjalizuje monitoring baterii
 * @return ESP_OK jeśli sukces
 */
esp_err_t battery_monitor_init(void);

/**
 * @brief Odczytuje dane baterii
 * @param data Struktura do zapisu danych baterii
 * @return ESP_OK jeśli sukces
 */
esp_err_t battery_monitor_read(battery_data_t *data);

/**
 * @brief Odczytuje napięcie baterii
 * @return Napięcie w woltach
 */
float battery_monitor_get_voltage(void);

/**
 * @brief Odczytuje procentowy poziom baterii
 * @return Poziom baterii 0-100%
 */
uint8_t battery_monitor_get_percentage(void);

/**
 * @brief Sprawdza czy bateria ma niski poziom
 * @return true jeśli bateria ma niski poziom
 */
bool battery_monitor_is_low_battery(void);

/**
 * @brief Czyści zasoby monitoringu baterii
 */
void battery_monitor_cleanup(void);

#endif // BATTERY_MONITOR_H
