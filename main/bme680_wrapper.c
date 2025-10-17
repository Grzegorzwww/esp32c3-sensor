#include "bme680_wrapper.h"
#include "bme680.h"
#include "bme680_platform.h"
#include "esp8266_wrapper.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "BME680";

// Global sensor handle
static bme680_sensor_t* sensor = NULL;

// SPI configuration for XIAO ESP32-C3
#define SPI_BUS       SPI2_HOST
#define SPI_SCK_GPIO  8    // D8
#define SPI_MOSI_GPIO 10   // D10
#define SPI_MISO_GPIO 9    // D9  
#define SPI_CS_GPIO   20   // D0

esp_err_t bme680_init(void)
{
    ESP_LOGI(TAG, "🌡️ Initializing BME680 sensor using dedicated ESP-IDF library...");
    ESP_LOGI(TAG, "📍 SPI Configuration:");
    ESP_LOGI(TAG, "   MOSI: GPIO%d (D10) → SDA", SPI_MOSI_GPIO);
    ESP_LOGI(TAG, "   MISO: GPIO%d (D9)  → SDO", SPI_MISO_GPIO);
    ESP_LOGI(TAG, "   SCK:  GPIO%d (D8)  → SCL", SPI_SCK_GPIO);
    ESP_LOGI(TAG, "   CS:   GPIO%d (D0)  → CSB", SPI_CS_GPIO);

    // Initialize SPI bus with esp8266_wrapper
    if (!spi_bus_init(SPI_BUS, SPI_SCK_GPIO, SPI_MISO_GPIO, SPI_MOSI_GPIO)) {
        ESP_LOGE(TAG, "❌ Failed to initialize SPI bus");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "✅ SPI bus initialized successfully");

    // Initialize BME680 sensor (SPI mode)
    // For SPI: bus=SPI_BUS, addr=0 (unused), cs=CS_GPIO
    ESP_LOGI(TAG, "🔄 Calling bme680_init_sensor(bus=%d, addr=0, cs=%d)...", SPI_BUS, SPI_CS_GPIO);
    sensor = bme680_init_sensor(SPI_BUS, 0, SPI_CS_GPIO);
    if (sensor == NULL) {
        ESP_LOGE(TAG, "❌ Failed to initialize BME680 sensor");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "✅ BME680 sensor initialized successfully");

    // Configure oversampling rates:
    // Temperature: 4x oversampling for better accuracy
    // Pressure: 2x oversampling (good balance of accuracy and speed)
    // Humidity: 2x oversampling 
    ESP_LOGI(TAG, "🔄 Setting oversampling rates...");
    if (!bme680_set_oversampling_rates(sensor, osr_4x, osr_2x, osr_2x)) {
        ESP_LOGE(TAG, "❌ Failed to set oversampling rates");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "⚙️ Oversampling: Temp=4x, Press=2x, Hum=2x");

    // Set IIR filter size for stable readings
    ESP_LOGI(TAG, "🔄 Setting filter size...");
    if (!bme680_set_filter_size(sensor, iir_size_3)) {
        ESP_LOGE(TAG, "❌ Failed to set filter size");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "🔧 IIR Filter size: 3");

    // Configure heater profile for gas measurements
    // Temperature: 320°C, Duration: 250ms (dłużej dla lepszego rozgrzania)
    ESP_LOGI(TAG, "🔄 Setting heater profile...");
    if (!bme680_set_heater_profile(sensor, 0, 320, 250)) {
        ESP_LOGW(TAG, "⚠️ Failed to set heater profile - skipping gas measurements");
    } else {
        // Use heater profile 0
        ESP_LOGI(TAG, "🔄 Using heater profile...");
        if (!bme680_use_heater_profile(sensor, 0)) {
            ESP_LOGW(TAG, "⚠️ Failed to use heater profile - skipping gas measurements");
        } else {
            ESP_LOGI(TAG, "🔥 Heater profile: 320°C, 250ms (extended for better gas reading)");
        }
    }

    ESP_LOGI(TAG, "🎉 BME680 initialization completed successfully!");
    return ESP_OK;
}

esp_err_t bme680_read_data(bme680_data_t *data)
{
    if (sensor == NULL || data == NULL) {
        ESP_LOGE(TAG, "❌ Sensor not initialized or null data pointer");
        return ESP_ERR_INVALID_STATE;
    }

    // Clear the data structure
    memset(data, 0, sizeof(bme680_data_t));

    // Start forced measurement
    if (!bme680_force_measurement(sensor)) {
        ESP_LOGE(TAG, "❌ Failed to start measurement");
        data->valid = false;
        return ESP_FAIL;
    }

    // Get measurement duration
    uint32_t duration = bme680_get_measurement_duration(sensor);
    ESP_LOGD(TAG, "⏱️ Measurement duration: %lu ms", duration);

    // Wait for measurement to complete - dodatkowy czas dla gazu
    ESP_LOGI(TAG, "⏳ Waiting for measurement completion (gas needs time to stabilize)...");
    vTaskDelay(pdMS_TO_TICKS(duration + 50)); // +50ms dla gazu

    // Check if measurement is still running
    if (bme680_is_measuring(sensor)) {
        ESP_LOGW(TAG, "⚠️ Sensor still measuring, waiting longer for gas stabilization...");
        vTaskDelay(pdMS_TO_TICKS(200)); // Więcej czasu dla gazu
        if (bme680_is_measuring(sensor)) {
            ESP_LOGE(TAG, "❌ Measurement timeout");
            data->valid = false;
            return ESP_FAIL;
        }
    }

    // Read results as float values
    bme680_values_float_t results;
    if (!bme680_get_results_float(sensor, &results)) {
        ESP_LOGE(TAG, "❌ Failed to read measurement results");
        data->valid = false;
        return ESP_FAIL;
    }

    // Copy results to our data structure
    data->temperature = results.temperature;
    data->pressure = results.pressure; // Already in hPa according to library docs
    data->humidity = results.humidity;
    data->gas_resistance = results.gas_resistance;

    // Check gas measurement validity - bardziej restrykcyjne sprawdzenie
    data->gas_valid = (data->gas_resistance > 100);  // Minimum 100 Ohm dla valid
    data->valid = true;

    // Debug gas sensor status
    ESP_LOGI(TAG, "🔍 Gas sensor debug:");
    ESP_LOGI(TAG, "   Raw gas resistance: %.0f Ohms", data->gas_resistance);
    ESP_LOGI(TAG, "   Gas reading valid: %s", data->gas_valid ? "YES" : "NO");
    if (!data->gas_valid) {
        ESP_LOGW(TAG, "   ⚠️ Gas reading invalid - possible causes:");
        ESP_LOGW(TAG, "     1. Heater still warming up (needs 2-3 measurements)");
        ESP_LOGW(TAG, "     2. Poor air circulation");
        ESP_LOGW(TAG, "     3. Sensor needs more time after deep sleep");
    }

    ESP_LOGD(TAG, "📊 Measurement results:");
    ESP_LOGD(TAG, "   Temperature: %.2f °C", data->temperature);
    ESP_LOGD(TAG, "   Pressure: %.2f hPa", data->pressure);
    ESP_LOGD(TAG, "   Humidity: %.2f %%RH", data->humidity);
    ESP_LOGD(TAG, "   Gas resistance: %.0f Ohms", data->gas_resistance);

    return ESP_OK;
}

void bme680_cleanup(void)
{
    if (sensor != NULL) {
        ESP_LOGI(TAG, "🧹 Cleaning up BME680 sensor");
        // The library handles cleanup internally
        sensor = NULL;
    }
}
