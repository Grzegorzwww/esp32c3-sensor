#include "BMP280.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "BMP280";

// Global variables
static spi_device_handle_t spi_device = NULL;
static bmp280_calib_data_t calib_data;
static bool initialized = false;
static int32_t t_fine = 0;  // Used for pressure compensation

// Helper function to read register via SPI
static esp_err_t bmp280_read_register(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (!initialized || spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t transaction = {0};
    uint8_t tx_data[len + 1];
    uint8_t rx_data[len + 1];

    // First byte is register address with read bit set (MSB = 1)
    tx_data[0] = reg_addr | 0x80;
    for (int i = 1; i <= len; i++) {
        tx_data[i] = 0x00;  // Dummy bytes
    }

    transaction.length = (len + 1) * 8;  // Total bits
    transaction.tx_buffer = tx_data;
    transaction.rx_buffer = rx_data;

    esp_err_t ret = spi_device_transmit(spi_device, &transaction);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Copy received data (skip first byte which is dummy)
    memcpy(data, &rx_data[1], len);
    return ESP_OK;
}

// Helper function to write register via SPI
static esp_err_t bmp280_write_register(uint8_t reg_addr, uint8_t data)
{
    if (!initialized || spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t transaction = {0};
    uint8_t tx_data[2];

    // First byte is register address with write bit (MSB = 0)
    tx_data[0] = reg_addr & 0x7F;
    tx_data[1] = data;

    transaction.length = 16;  // 2 bytes = 16 bits
    transaction.tx_buffer = tx_data;

    esp_err_t ret = spi_device_transmit(spi_device, &transaction);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

// Read calibration data from sensor
static esp_err_t bmp280_read_calibration_data(void)
{
    uint8_t calib_raw[24];
    esp_err_t ret = bmp280_read_register(BMP280_REG_DIG_T1, calib_raw, 24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data");
        return ret;
    }

    // Parse calibration data (little endian)
    calib_data.dig_T1 = (calib_raw[1] << 8) | calib_raw[0];
    calib_data.dig_T2 = (calib_raw[3] << 8) | calib_raw[2];
    calib_data.dig_T3 = (calib_raw[5] << 8) | calib_raw[4];
    calib_data.dig_P1 = (calib_raw[7] << 8) | calib_raw[6];
    calib_data.dig_P2 = (calib_raw[9] << 8) | calib_raw[8];
    calib_data.dig_P3 = (calib_raw[11] << 8) | calib_raw[10];
    calib_data.dig_P4 = (calib_raw[13] << 8) | calib_raw[12];
    calib_data.dig_P5 = (calib_raw[15] << 8) | calib_raw[14];
    calib_data.dig_P6 = (calib_raw[17] << 8) | calib_raw[16];
    calib_data.dig_P7 = (calib_raw[19] << 8) | calib_raw[18];
    calib_data.dig_P8 = (calib_raw[21] << 8) | calib_raw[20];
    calib_data.dig_P9 = (calib_raw[23] << 8) | calib_raw[22];

    ESP_LOGI(TAG, "✅ Calibration data loaded");
    ESP_LOGD(TAG, "T1=%u, T2=%d, T3=%d", calib_data.dig_T1, calib_data.dig_T2, calib_data.dig_T3);
    ESP_LOGD(TAG, "P1=%u, P2=%d, P3=%d", calib_data.dig_P1, calib_data.dig_P2, calib_data.dig_P3);

    return ESP_OK;
}

// Compensate temperature (returns temperature in DegC, float)
static float bmp280_compensate_temperature(int32_t adc_T)
{
    int32_t var1, var2;
    
    var1 = ((((adc_T >> 3) - ((int32_t)calib_data.dig_T1 << 1))) * ((int32_t)calib_data.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib_data.dig_T1)) * ((adc_T >> 4) - ((int32_t)calib_data.dig_T1))) >> 12) * ((int32_t)calib_data.dig_T3)) >> 14;
    
    t_fine = var1 + var2;
    
    float temperature = (t_fine * 5 + 128) >> 8;
    return temperature / 100.0f;
}

// Compensate pressure (returns pressure in Pa, float)
static float bmp280_compensate_pressure(int32_t adc_P)
{
    int64_t var1, var2, p;
    
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib_data.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib_data.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib_data.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib_data.dig_P3) >> 8) + ((var1 * (int64_t)calib_data.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calib_data.dig_P1) >> 33;
    
    if (var1 == 0) {
        return 0;  // Avoid exception caused by division by zero
    }
    
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib_data.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib_data.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib_data.dig_P7) << 4);
    
    return (float)p / 256.0f;
}

// Public functions implementation

esp_err_t bmp280_init(const bmp280_config_t *config)
{
    if (initialized) {
        ESP_LOGW(TAG, "BMP280 already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🔧 Initializing BMP280 sensor...");
    ESP_LOGI(TAG, "📍 Pin configuration:");
    ESP_LOGI(TAG, "   MISO (SDD): GPIO%d", BMP280_PIN_MISO);
    ESP_LOGI(TAG, "   MOSI (SDA): GPIO%d", BMP280_PIN_MOSI);
    ESP_LOGI(TAG, "   SCK  (SCL): GPIO%d", BMP280_PIN_CLK);
    ESP_LOGI(TAG, "   CS   (CSB): GPIO%d", BMP280_PIN_CS);

    // Configure SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = BMP280_PIN_MISO,
        .mosi_io_num = BMP280_PIN_MOSI,
        .sclk_io_num = BMP280_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };

    esp_err_t ret = spi_bus_initialize(BMP280_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {  // ESP_ERR_INVALID_STATE means bus already initialized
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,  // 1 MHz
        .mode = 0,                  // SPI mode 0
        .spics_io_num = BMP280_PIN_CS,
        .queue_size = 7,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
    };

    ret = spi_bus_add_device(BMP280_SPI_HOST, &devcfg, &spi_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        spi_bus_free(BMP280_SPI_HOST);
        return ret;
    }

    initialized = true;

    // Small delay for sensor to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    // Test SPI communication first
    ESP_LOGI(TAG, "🔍 Testing SPI communication...");
    
    // Check if sensor is responding
    if (!bmp280_is_connected()) {
        ESP_LOGE(TAG, "❌ BMP280 not found or not responding");
        ESP_LOGE(TAG, "💡 Check connections:");
        ESP_LOGE(TAG, "   VCC  → 3.3V");
        ESP_LOGE(TAG, "   GND  → GND");
        ESP_LOGE(TAG, "   SDD  → GPIO%d (MISO)", BMP280_PIN_MISO);
        ESP_LOGE(TAG, "   SDA  → GPIO%d (MOSI)", BMP280_PIN_MOSI);
        ESP_LOGE(TAG, "   SCL  → GPIO%d (SCK)", BMP280_PIN_CLK);
        ESP_LOGE(TAG, "   CSB  → GPIO%d (CS)", BMP280_PIN_CS);
        bmp280_deinit();
        return ESP_ERR_NOT_FOUND;
    }

    // Read calibration data
    ret = bmp280_read_calibration_data();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to read calibration data");
        bmp280_deinit();
        return ret;
    }

    // Configure sensor
    uint8_t ctrl_meas = (config->temp_oversampling << 5) | 
                        (config->press_oversampling << 2) | 
                        config->mode;
    
    uint8_t config_reg = (config->standby_time << 5) | 
                         (config->filter << 2);

    ret = bmp280_write_register(BMP280_REG_CONFIG, config_reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to write config register");
        bmp280_deinit();
        return ret;
    }

    ret = bmp280_write_register(BMP280_REG_CTRL_MEAS, ctrl_meas);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to write control register");
        bmp280_deinit();
        return ret;
    }

    ESP_LOGI(TAG, "✅ BMP280 initialized successfully");
    ESP_LOGI(TAG, "📊 Mode: %s, Temp OS: %dx, Press OS: %dx", 
             config->mode == BMP280_MODE_NORMAL ? "NORMAL" : 
             config->mode == BMP280_MODE_FORCED ? "FORCED" : "SLEEP",
             1 << config->temp_oversampling,
             1 << config->press_oversampling);

    return ESP_OK;
}

esp_err_t bmp280_deinit(void)
{
    if (!initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🧹 Deinitializing BMP280...");

    if (spi_device != NULL) {
        spi_bus_remove_device(spi_device);
        spi_device = NULL;
    }
    
    spi_bus_free(BMP280_SPI_HOST);
    initialized = false;

    ESP_LOGI(TAG, "✅ BMP280 deinitialized");
    return ESP_OK;
}

bool bmp280_is_connected(void)
{
    if (!initialized) {
        return false;
    }

    uint8_t chip_id = 0;
    esp_err_t ret = bmp280_read_register(BMP280_REG_ID, &chip_id, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        return false;
    }

    bool connected = (chip_id == BMP280_CHIP_ID);
    ESP_LOGI(TAG, "🔍 Chip ID: 0x%02X %s", chip_id, 
             connected ? "(BMP280 detected)" : "(Unknown chip)");
    
    return connected;
}

esp_err_t bmp280_read_measurements(bmp280_data_t *data)
{
    if (!initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read raw pressure and temperature data (6 bytes)
    uint8_t raw_data[6];
    esp_err_t ret = bmp280_read_register(BMP280_REG_PRESS_MSB, raw_data, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read measurement data");
        return ret;
    }

    // Combine bytes to get raw ADC values
    int32_t adc_P = ((uint32_t)raw_data[0] << 12) | ((uint32_t)raw_data[1] << 4) | (raw_data[2] >> 4);
    int32_t adc_T = ((uint32_t)raw_data[3] << 12) | ((uint32_t)raw_data[4] << 4) | (raw_data[5] >> 4);

    // Compensate temperature (must be done first for pressure compensation)
    data->temperature = bmp280_compensate_temperature(adc_T);
    
    // Compensate pressure (in Pa, convert to hPa)
    float pressure_pa = bmp280_compensate_pressure(adc_P);
    data->pressure = pressure_pa / 100.0f;  // Convert Pa to hPa
    
    // Calculate altitude
    data->altitude = bmp280_calculate_altitude(data->pressure, 1013.25f);

    ESP_LOGD(TAG, "📊 T: %.2f°C, P: %.2f hPa, Alt: %.1f m", 
             data->temperature, data->pressure, data->altitude);

    return ESP_OK;
}

esp_err_t bmp280_read_temperature(float *temperature)
{
    if (!initialized || temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw_data[3];
    esp_err_t ret = bmp280_read_register(BMP280_REG_TEMP_MSB, raw_data, 3);
    if (ret != ESP_OK) {
        return ret;
    }

    int32_t adc_T = ((uint32_t)raw_data[0] << 12) | ((uint32_t)raw_data[1] << 4) | (raw_data[2] >> 4);
    *temperature = bmp280_compensate_temperature(adc_T);

    return ESP_OK;
}

esp_err_t bmp280_read_pressure(float *pressure)
{
    if (!initialized || pressure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Need to read temperature first for pressure compensation
    float temp;
    esp_err_t ret = bmp280_read_temperature(&temp);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t raw_data[3];
    ret = bmp280_read_register(BMP280_REG_PRESS_MSB, raw_data, 3);
    if (ret != ESP_OK) {
        return ret;
    }

    int32_t adc_P = ((uint32_t)raw_data[0] << 12) | ((uint32_t)raw_data[1] << 4) | (raw_data[2] >> 4);
    float pressure_pa = bmp280_compensate_pressure(adc_P);
    *pressure = pressure_pa / 100.0f;  // Convert Pa to hPa

    return ESP_OK;
}

void bmp280_get_default_config(bmp280_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->mode = BMP280_MODE_NORMAL;
    config->temp_oversampling = BMP280_OVERSAMPLING_2X;
    config->press_oversampling = BMP280_OVERSAMPLING_16X;
    config->filter = BMP280_FILTER_4;
    config->standby_time = BMP280_STANDBY_125_MS;
}

float bmp280_calculate_altitude(float pressure, float sea_level_pressure)
{
    if (pressure <= 0) {
        return NAN;
    }
    
    // Standard atmosphere formula
    return 44330.0f * (1.0f - powf(pressure / sea_level_pressure, 0.1903f));
}
