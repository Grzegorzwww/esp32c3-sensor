#include "lm35.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "LM35";

// Zmienne globalne
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool initialized = false;
static lm35_config_t current_config;
static float calibration_offset = 0.0f;



void sensor_start()
{

    
    // Konfiguracja LM35 - użyj domyślnej konfiguracji
    lm35_config_t config_D2 = {
        .adc_unit = ADC_UNIT_1,
        .adc_channel = ADC_CHANNEL_2,    // GPIO2
        .attenuation = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .voltage_reference_mv = 3300.0f
    };
    
    // Inicjalizacja czujnika LM35
    esp_err_t ret = lm35_init(&config_D2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LM35 INIT ERROR - Check GPIO2 connection");
        return;
    }


    // Konfiguracja LM35 - użyj domyślnej konfiguracji
    lm35_config_t config_D1 = {
        .adc_unit = ADC_UNIT_1,
        .adc_channel = ADC_CHANNEL_1,    // GPIO1
        .attenuation = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .voltage_reference_mv = 3300.0f
    };
    
        // Inicjalizacja czujnika LM35
    ret = lm35_init(&config_D1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LM35 INIT ERROR - Check GPIO1 connection");
        return;
    }

    
    ESP_LOGI(TAG, "LM35 Ready - Reading every 5s...");


    

}
// Funkcje pomocnicze
static esp_err_t lm35_adc_calibration_init(void)
{
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "🔧 Calibration scheme version is Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = current_config.adc_unit,
            .atten = current_config.attenuation,
            .bitwidth = current_config.bitwidth,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "🔧 Calibration scheme version is Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = current_config.adc_unit,
            .atten = current_config.attenuation,
            .bitwidth = current_config.bitwidth,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ ADC calibration initialized");
    } else {
        ESP_LOGW(TAG, "⚠️ ADC calibration failed, using raw values");
        adc_cali_handle = NULL;
    }
    
    return ret;
}

static void lm35_adc_calibration_deinit(void)
{
    if (adc_cali_handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(adc_cali_handle);
#endif
        adc_cali_handle = NULL;
        ESP_LOGI(TAG, "🧹 ADC calibration deinitialized");
    }
}

// Implementacje funkcji publicznych

esp_err_t lm35_init(const lm35_config_t *config)
{
    // if (initialized) {
    //     ESP_LOGW(TAG, "LM35 already initialized");
    //     return ESP_OK;
    // }

    ESP_LOGI(TAG, "🌡️ Initializing LM35 temperature sensor...");

    // Ustaw konfigurację
    if (config != NULL) {
        current_config = *config;
    } else {
        lm35_get_default_config(&current_config);
    }

    ESP_LOGI(TAG, "📍 Configuration:");
    ESP_LOGI(TAG, "   ADC Unit: %d", current_config.adc_unit);
    ESP_LOGI(TAG, "   ADC Channel: %d ", current_config.adc_channel);
    ESP_LOGI(TAG, "   Attenuation: %d", current_config.attenuation);
    ESP_LOGI(TAG, "   Bitwidth: %d", current_config.bitwidth);
    ESP_LOGI(TAG, "   Reference Voltage: %.1f mV", current_config.voltage_reference_mv);

    // Konfiguracja ADC OneShot
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = current_config.adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    // Konfiguracja kanału ADC
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = current_config.bitwidth,
        .atten = current_config.attenuation,
    };

    ret = adc_oneshot_config_channel(adc_handle, current_config.adc_channel, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }

    // Inicjalizacja kalibracji ADC
    lm35_adc_calibration_init();

    initialized = true;
    ESP_LOGI(TAG, "✅ LM35 sensor initialized successfully");
    ESP_LOGI(TAG, "🔥 Ready to measure temperature every 5 seconds!");

    return ESP_OK;
}

esp_err_t lm35_deinit(void)
{
    if (!initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🧹 Deinitializing LM35 sensor...");

    lm35_adc_calibration_deinit();

    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    initialized = false;
    ESP_LOGI(TAG, "✅ LM35 sensor deinitialized");
    return ESP_OK;
}

esp_err_t lm35_read_temperature(lm35_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wyczyść strukturę danych
    memset(data, 0, sizeof(lm35_data_t));

    // Odczyt surowej wartości ADC
    int raw_value;
    esp_err_t ret = adc_oneshot_read(adc_handle, current_config.adc_channel, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to read ADC: %s", esp_err_to_name(ret));
        data->valid = false;
        return ret;
    }

    data->raw_adc_value = raw_value;

    // Konwersja na napięcie
    if (adc_cali_handle) {
        // Użyj skalibrowanej konwersji
        int voltage_mv;
        ret = adc_cali_raw_to_voltage(adc_cali_handle, raw_value, &voltage_mv);
        if (ret == ESP_OK) {
            data->voltage_mv = (float)voltage_mv;
        } else {
            ESP_LOGW(TAG, "⚠️ Calibration failed, using raw calculation");
            data->voltage_mv = ((float)raw_value / LM35_ADC_MAX_VALUE) * current_config.voltage_reference_mv;
        }
    } else {
        // Użyj surowego przeliczenia
        data->voltage_mv = ((float)raw_value / LM35_ADC_MAX_VALUE) * current_config.voltage_reference_mv;
    }

    // Konwersja napięcia na temperaturę (LM35: 10mV/°C)
    data->temperature_celsius = (data->voltage_mv / LM35_MV_PER_DEGREE) + calibration_offset;
    
    data->valid = true;

    ESP_LOGD(TAG, "📊 Raw: %d, Voltage: %.2f mV, Temp: %.2f°C", 
             data->raw_adc_value, data->voltage_mv, data->temperature_celsius);

    return ESP_OK;
}

bool lm35_is_initialized(void)
{
    return initialized;
}

void lm35_get_default_config(lm35_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->adc_unit = LM35_ADC_UNIT;
    config->adc_channel = LM35_ADC_CHANNEL;
    config->attenuation = LM35_ADC_ATTEN;
    config->bitwidth = LM35_ADC_BITWIDTH;
    config->voltage_reference_mv = LM35_VREF_MV;
}

esp_err_t lm35_set_calibration_offset(float offset_celsius)
{
    calibration_offset = offset_celsius;
    ESP_LOGI(TAG, "🔧 Calibration offset set to: %.2f°C", offset_celsius);
    return ESP_OK;
}
