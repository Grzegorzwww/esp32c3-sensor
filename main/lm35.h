#ifndef LM35_H
#define LM35_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

// Konfiguracja LM35
#define LM35_ADC_UNIT           ADC_UNIT_1       // ESP32-C3 ma tylko ADC1
#define LM35_ADC_CHANNEL        ADC_CHANNEL_3    // GPIO3 (D3) = ADC1_CH3
#define LM35_ADC_ATTEN          ADC_ATTEN_DB_12  // 0-3.3V range
#define LM35_ADC_BITWIDTH       ADC_BITWIDTH_12  // 12-bit resolution (0-4095)

// LM35 parametry
#define LM35_MV_PER_DEGREE      10.0f   // LM35 output: 10mV/°C
#define LM35_VREF_MV           3300.0f  // Reference voltage in mV
#define LM35_ADC_MAX_VALUE     4095.0f  // 12-bit ADC max value

// Struktura danych LM35
typedef struct {
    float temperature_celsius;
    float voltage_mv;
    int raw_adc_value;
    bool valid;
} lm35_data_t;

// Konfiguracja LM35
typedef struct {
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    adc_atten_t attenuation;
    adc_bitwidth_t bitwidth;
    float voltage_reference_mv;
} lm35_config_t;

/**
 * @brief Inicjalizacja czujnika LM35
 * @param config Konfiguracja czujnika (może być NULL dla domyślnej)
 * @return ESP_OK przy sukcesie
 */
esp_err_t lm35_init(const lm35_config_t *config);

/**
 * @brief Zwolnienie zasobów czujnika LM35
 * @return ESP_OK przy sukcesie
 */
esp_err_t lm35_deinit(void);

/**
 * @brief Odczyt temperatury z czujnika LM35
 * @param data Struktura do zapisania wyników
 * @return ESP_OK przy sukcesie
 */
esp_err_t lm35_read_temperature(lm35_data_t *data);

/**
 * @brief Sprawdzenie czy czujnik jest zainicjalizowany
 * @return true jeśli zainicjalizowany
 */
bool lm35_is_initialized(void);

/**
 * @brief Pobierz domyślną konfigurację
 * @param config Wskaźnik na strukturę konfiguracji
 */
void lm35_get_default_config(lm35_config_t *config);

/**
 * @brief Kalibracja czujnika (opcjonalna)
 * @param offset_celsius Przesunięcie w stopniach Celsjusza
 * @return ESP_OK przy sukcesie
 */
esp_err_t lm35_set_calibration_offset(float offset_celsius);

#endif // LM35_H
