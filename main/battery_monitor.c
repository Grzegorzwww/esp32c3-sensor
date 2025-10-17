#include "battery_monitor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "BATTERY";

// Zmienne globalne
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool adc_calibrated = false;
static bool module_initialized = false;

// Inicjalizacja kalibracji ADC
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "📊 ADC calibration scheme version is Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "📊 ADC calibration scheme version is Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ ADC calibration success");
    } else {
        ESP_LOGW(TAG, "⚠️ ADC calibration failed, using raw values");
    }
    return calibrated;
}

// Konwersja napięcia na procent baterii
static int voltage_to_percentage(float voltage)
{
    // Prawidłowa charakterystyka Li-Ion 18650:
    // 4.2V = 100% (pełne naładowanie)
    // 4.0V = 85%  (bardzo dobre)
    // 3.9V = 75%  (dobre)
    // 3.8V = 60%  (średnie)
    // 3.7V = 40%  (ok, nominalne napięcie)
    // 3.6V = 25%  (niskie)
    // 3.4V = 10%  (bardzo niskie)
    // 3.0V = 0%   (rozładowane)
    
    if (voltage >= 4.2f) return 100;
    if (voltage >= 4.0f) return 85 + (int)((voltage - 4.0f) * 75);  // 85-100%
    if (voltage >= 3.9f) return 75 + (int)((voltage - 3.9f) * 100); // 75-85%
    if (voltage >= 3.8f) return 60 + (int)((voltage - 3.8f) * 150); // 60-75%
    if (voltage >= 3.7f) return 40 + (int)((voltage - 3.7f) * 200); // 40-60%
    if (voltage >= 3.6f) return 25 + (int)((voltage - 3.6f) * 150); // 25-40%
    if (voltage >= 3.4f) return 10 + (int)((voltage - 3.4f) * 75);  // 10-25%
    if (voltage >= 3.0f) return (int)((voltage - 3.0f) * 25);       // 0-10%
    
    return 0;  // Poniżej 3.0V = całkowicie rozładowane
}

esp_err_t battery_monitor_init(void)
{
    if (module_initialized) {
        ESP_LOGW(TAG, "⚠️ Battery monitor already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🔋 Initializing battery monitor");
    ESP_LOGI(TAG, "📐 Voltage divider config:");
    ESP_LOGI(TAG, "   R1 = %d Ω (upper resistor)", BATTERY_R1_OHMS);
    ESP_LOGI(TAG, "   R2 = %d Ω (lower resistor)", BATTERY_R2_OHMS);
    ESP_LOGI(TAG, "   Ratio = %.1f (measures up to %.1fV safely)", 
             BATTERY_DIVIDER_RATIO, 3.3f * BATTERY_DIVIDER_RATIO);
    ESP_LOGI(TAG, "📍 ADC Pin: GPIO2 (A0)");

    // Konfiguracja ADC
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    // Konfiguracja kanału ADC
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = BATTERY_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(adc1_handle, BATTERY_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to config ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc1_handle);
        return ret;
    }

    // Inicjalizacja kalibracji ADC
    adc_calibrated = adc_calibration_init(ADC_UNIT_1, BATTERY_ADC_CHANNEL, BATTERY_ADC_ATTEN, &adc1_cali_handle);

    module_initialized = true;
    ESP_LOGI(TAG, "✅ Battery monitor initialized");
    
    return ESP_OK;
}

esp_err_t battery_monitor_read(battery_data_t *data)
{
    if (!module_initialized || !data) {
        ESP_LOGE(TAG, "❌ Battery monitor not initialized or invalid data pointer");
        return ESP_ERR_INVALID_STATE;
    }

    // Wyzeruj dane
    memset(data, 0, sizeof(battery_data_t));

    // Odczyt surowej wartości ADC
    int adc_raw;
    esp_err_t ret = adc_oneshot_read(adc1_handle, BATTERY_ADC_CHANNEL, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ ADC read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Konwersja na napięcie
    int voltage_mv = 0;
    if (adc_calibrated) {
        ret = adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ ADC calibration failed, using approximation");
            // Przybliżenie: 3.3V / 4095 = 0.806mV per unit
            voltage_mv = (adc_raw * 3300) / 4095;
        }
    } else {
        // Przybliżenie bez kalibracji
        voltage_mv = (adc_raw * 3300) / 4095;
    }

    // Przelicz napięcie przez dzielnik
    float measured_voltage = voltage_mv / 1000.0f;  // mV -> V
    data->voltage = measured_voltage * BATTERY_DIVIDER_RATIO;  // Napięcie rzeczywiste baterii

    // Debug: wyświetl surowe wartości
    ESP_LOGI(TAG, "🔍 ADC Debug:");
    ESP_LOGI(TAG, "   Raw ADC: %d (0-4095)", adc_raw);
    ESP_LOGI(TAG, "   Voltage mV: %d mV", voltage_mv);
    ESP_LOGI(TAG, "   Measured: %.3fV (after ADC)", measured_voltage);
    ESP_LOGI(TAG, "   Final: %.3fV (after x%.1f divider)", data->voltage, BATTERY_DIVIDER_RATIO);

    // Oblicz procent baterii
    data->percentage = voltage_to_percentage(data->voltage);
    
    // Alert o błędnym napięciu (powyżej normalnego zakresu baterii)
    if (data->voltage > 4.5f) {
        ESP_LOGW(TAG, "⚠️ VOLTAGE TOO HIGH! (%.2fV > 4.5V)", data->voltage);
        ESP_LOGW(TAG, "   Possible issues:");
        ESP_LOGW(TAG, "   1. Voltage divider not connected properly");
        ESP_LOGW(TAG, "   2. Measuring VCC instead of battery");
        ESP_LOGW(TAG, "   3. Wrong resistor values");
        data->low_battery = false;  // Nie jest to niski poziom, to błąd pomiaru
    } else {
        // Niski poziom baterii: poniżej 3.6V (około 25%)
        data->low_battery = (data->voltage < 3.6f);
    }
    
    data->valid = true;

    ESP_LOGI(TAG, "🔋 Battery: %.2fV (%d%%) %s", 
             data->voltage, data->percentage, 
             data->low_battery ? "⚠️ LOW" : "✅ OK");
    
    // Dodatkowe info o stanie baterii
    const char* battery_status = 
        data->voltage >= 4.0f ? "Excellent" :
        data->voltage >= 3.8f ? "Good" :
        data->voltage >= 3.7f ? "Fair" :
        data->voltage >= 3.6f ? "Low" : "Critical";
    
    ESP_LOGI(TAG, "📊 Battery status: %s (3.98V = ~75-80%% for Li-Ion)", battery_status);

    return ESP_OK;
}

void battery_monitor_cleanup(void)
{
    ESP_LOGI(TAG, "🧹 Cleaning up battery monitor");

    if (adc_calibrated && adc1_cali_handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(adc1_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(adc1_cali_handle);
#endif
        adc1_cali_handle = NULL;
        adc_calibrated = false;
    }

    if (adc1_handle) {
        adc_oneshot_del_unit(adc1_handle);
        adc1_handle = NULL;
    }

    module_initialized = false;
    ESP_LOGI(TAG, "✅ Battery monitor cleanup completed");
}

float battery_monitor_get_voltage(void)
{
    battery_data_t data;
    esp_err_t ret = battery_monitor_read(&data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery voltage");
        return 0.0f;
    }
    return data.voltage;
}

uint8_t battery_monitor_get_percentage(void)
{
    battery_data_t data;
    esp_err_t ret = battery_monitor_read(&data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery percentage");
        return 0;
    }
    return data.percentage;
}

bool battery_monitor_is_low_battery(void)
{
    battery_data_t data;
    esp_err_t ret = battery_monitor_read(&data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check battery status");
        return true; // Bezpieczne założenie - traktuj jako niski poziom
    }
    return data.low_battery;
}
