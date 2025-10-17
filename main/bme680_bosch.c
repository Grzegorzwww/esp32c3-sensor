#include "bme680.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BME680";

// SPI configuration
#define BME680_MOSI_PIN    GPIO_NUM_10   // D10 → SDA (MOSI)
#define BME680_MISO_PIN    GPIO_NUM_9    // D9  → SDO (MISO)  
#define BME680_SCK_PIN     GPIO_NUM_8    // D8  → SCL (SCLK)
#define BME680_CS_PIN      GPIO_NUM_20   // D0  → CS
#define BME680_SPI_CLOCK   1000000       // 1MHz SPI clock

// Global variables
static spi_device_handle_t spi_handle;
static struct bme68x_dev bme;

// SPI read/write functions for the BME68x library
static BME68X_INTF_RET_TYPE bme68x_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    if (spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI handle is NULL!");
        return BME68X_E_COM_FAIL;
    }

    ESP_LOGI(TAG, "📖 SPI Read: reg=0x%02X, len=%lu", reg_addr, len);

    spi_transaction_t trans = {
        .flags = SPI_TRANS_USE_TXDATA,
        .cmd = 0,
        .addr = 0,
        .length = 8,
        .rxlength = len * 8,
        .tx_data = {reg_addr | 0x80, 0, 0, 0}, // Set read bit
        .rx_buffer = reg_data
    };

    esp_err_t ret = spi_device_transmit(spi_handle, &trans);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ SPI Read success: 0x%02X", reg_data[0]);
    } else {
        ESP_LOGE(TAG, "❌ SPI Read failed: %s", esp_err_to_name(ret));
    }
    
    return (ret == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

static BME68X_INTF_RET_TYPE bme68x_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    if (spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI handle is NULL!");
        return BME68X_E_COM_FAIL;
    }

    ESP_LOGI(TAG, "📝 SPI Write: reg=0x%02X, data=0x%02X, len=%lu", reg_addr, reg_data[0], len);

    uint8_t *tx_buffer = malloc(len + 1);
    if (tx_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate TX buffer");
        return BME68X_E_COM_FAIL;
    }

    tx_buffer[0] = reg_addr & 0x7F; // Clear read bit
    memcpy(&tx_buffer[1], reg_data, len);

    spi_transaction_t trans = {
        .length = (len + 1) * 8,
        .tx_buffer = tx_buffer,
        .rx_buffer = NULL
    };

    esp_err_t ret = spi_device_transmit(spi_handle, &trans);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ SPI Write success");
    } else {
        ESP_LOGE(TAG, "❌ SPI Write failed: %s", esp_err_to_name(ret));
    }
    
    free(tx_buffer);
    
    return (ret == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

static void bme68x_delay_us(uint32_t period, void *intf_ptr)
{
    vTaskDelay(pdMS_TO_TICKS((period + 500) / 1000)); // Convert microseconds to ticks
}

esp_err_t bme680_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "🌡️ Initializing BME680 sensor over SPI...");
    ESP_LOGI(TAG, "📍 SPI Configuration:");
    ESP_LOGI(TAG, "   MOSI: GPIO%d (D10) → SDA", BME680_MOSI_PIN);
    ESP_LOGI(TAG, "   MISO: GPIO%d (D9)  → SDO", BME680_MISO_PIN);
    ESP_LOGI(TAG, "   SCK:  GPIO%d (D8)  → SCL", BME680_SCK_PIN);
    ESP_LOGI(TAG, "   CS:   GPIO%d (D0)  → CS", BME680_CS_PIN);

    // Initialize SPI bus
    spi_bus_config_t bus_config = {
        .mosi_io_num = BME680_MOSI_PIN,
        .miso_io_num = BME680_MISO_PIN,
        .sclk_io_num = BME680_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64
    };

    ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add device to SPI bus
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = BME680_SPI_CLOCK,
        .mode = 0,
        .spics_io_num = BME680_CS_PIN,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .flags = 0
    };

    ret = spi_bus_add_device(SPI2_HOST, &dev_config, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to SPI bus: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    // Initialize BME68x device structure
    bme.read = bme68x_spi_read;
    bme.write = bme68x_spi_write;
    bme.intf = BME68X_SPI_INTF;
    bme.delay_us = bme68x_delay_us;
    bme.intf_ptr = NULL;
    bme.amb_temp = 25; // Ambient temperature for calculation

    // Initialize the BME68x sensor
    int8_t rslt = bme68x_init(&bme);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "BME68x init failed with error: %d", rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✅ BME680 chip detected (ID: 0x%02X)", bme.chip_id);

    // Configure sensor settings
    struct bme68x_conf conf;
    conf.filter = BME68X_FILTER_OFF;
    conf.odr = BME68X_ODR_NONE;
    conf.os_hum = BME68X_OS_2X;   // 2x oversampling for humidity  
    conf.os_pres = BME68X_OS_4X;  // 4x oversampling for pressure
    conf.os_temp = BME68X_OS_8X;  // 8x oversampling for temperature

    rslt = bme68x_set_conf(&conf, &bme);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "Failed to set configuration: %d", rslt);
        return ESP_FAIL;
    }

    // Configure heater for gas sensor
    struct bme68x_heatr_conf heatr_conf;
    heatr_conf.enable = BME68X_ENABLE;
    heatr_conf.heatr_temp = 320;  // 320°C heater temperature
    heatr_conf.heatr_dur = 150;   // 150ms heater duration

    rslt = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &bme);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "Failed to set heater configuration: %d", rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "🔥 BME680 initialized successfully!");
    ESP_LOGI(TAG, "📊 Sensors: Temperature, Pressure, Humidity, Gas");

    return ESP_OK;
}

esp_err_t bme680_read_data(bme680_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set forced mode to trigger measurement
    int8_t rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &bme);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "Failed to set forced mode: %d", rslt);
        return ESP_FAIL;
    }

    // Calculate measurement duration
    struct bme68x_heatr_conf heatr_conf;
    heatr_conf.enable = BME68X_ENABLE;
    heatr_conf.heatr_temp = 320;
    heatr_conf.heatr_dur = 150;
    
    struct bme68x_conf conf;
    uint32_t del_period = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme) + (uint32_t)(heatr_conf.heatr_dur * 1000);
    
    // Wait for measurement to complete
    vTaskDelay(pdMS_TO_TICKS((del_period + 500) / 1000));

    // Read the sensor data
    struct bme68x_data sensor_data;
    uint8_t n_data = 0;
    
    rslt = bme68x_get_data(BME68X_FORCED_MODE, &sensor_data, &n_data, &bme);
    
    if (rslt != BME68X_OK || n_data == 0) {
        ESP_LOGE(TAG, "Failed to read sensor data: %d, n_data: %d", rslt, n_data);
        data->valid = false;
        return ESP_FAIL;
    }

    // Convert and scale data according to BME68x library format
    data->temperature = sensor_data.temperature;           // Already in °C
    data->pressure = sensor_data.pressure / 100.0f;       // Convert Pa to hPa
    data->humidity = sensor_data.humidity;                 // Already in %RH
    data->gas_resistance = sensor_data.gas_resistance;     // Already in Ohms
    data->gas_valid = (sensor_data.status & BME68X_GASM_VALID_MSK) ? true : false;
    data->valid = true;

    ESP_LOGI(TAG, "🔍 Sensor readings:");
    ESP_LOGI(TAG, "   Temperature: %.2f°C", data->temperature);
    ESP_LOGI(TAG, "   Pressure: %.2f hPa", data->pressure);
    ESP_LOGI(TAG, "   Humidity: %.2f%%RH", data->humidity);
    ESP_LOGI(TAG, "   Gas: %.0f Ohms (valid: %s)", data->gas_resistance, data->gas_valid ? "YES" : "NO");

    return ESP_OK;
}

void bme680_cleanup(void)
{
    if (spi_handle) {
        spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
    }
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "BME680 cleanup completed");
}
