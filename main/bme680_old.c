#include "bme680.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "BME680";

static spi_device_handle_t bme680_spi_handle = NULL;
static bme680_calib_data_t calib_data;

// SPI read/write functions
static esp_err_t bme680_spi_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    if (bme680_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    spi_transaction_t trans = {
        .cmd = 0,
        .addr = reg | 0x80,  // Read bit
        .length = len * 8,
        .rxlength = len * 8,
        .rx_buffer = data,
        .tx_buffer = NULL
    };
    
    return spi_device_transmit(bme680_spi_handle, &trans);
}

static esp_err_t bme680_spi_write_reg(uint8_t reg, uint8_t data)
{
    if (bme680_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t tx_data[2] = {reg & 0x7F, data};  // Clear read bit
    
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = NULL
    };
    
    return spi_device_transmit(bme680_spi_handle, &trans);
}

// Read calibration coefficients
static esp_err_t bme680_read_calibration(void)
{
    uint8_t coeff[42];
    esp_err_t ret;
    
    // Read temperature and pressure coefficients (0x89-0xA1)
    ret = bme680_spi_read_reg(BME680_COEFF_T1_LSB, &coeff[0], 25);
    if (ret != ESP_OK) return ret;
    
    // Read humidity coefficients (0xE1-0xE8) 
    ret = bme680_spi_read_reg(0xE1, &coeff[25], 8);
    if (ret != ESP_OK) return ret;
    
    // Parse temperature coefficients (Table 11)
    calib_data.par_t1 = (coeff[1] << 8) | coeff[0];  // 0x8A, 0x89
    calib_data.par_t2 = (coeff[3] << 8) | coeff[2];  // 0x8C, 0x8B  
    calib_data.par_t3 = coeff[4];                     // 0x8D
    
    // Parse pressure coefficients (Table 12)
    calib_data.par_p1 = (coeff[6] << 8) | coeff[5];   // 0x8F, 0x8E
    calib_data.par_p2 = (coeff[8] << 8) | coeff[7];   // 0x91, 0x90
    calib_data.par_p3 = coeff[9];                      // 0x92
    calib_data.par_p4 = (coeff[12] << 8) | coeff[11]; // 0x95, 0x94
    calib_data.par_p5 = (coeff[14] << 8) | coeff[13]; // 0x97, 0x96
    calib_data.par_p6 = coeff[16];                     // 0x99
    calib_data.par_p7 = coeff[15];                     // 0x98
    calib_data.par_p8 = (coeff[20] << 8) | coeff[19]; // 0x9D, 0x9C
    calib_data.par_p9 = (coeff[22] << 8) | coeff[21]; // 0x9F, 0x9E
    calib_data.par_p10 = coeff[23];                    // 0xA0
    
    // Parse humidity coefficients according to datasheet Table 13
    // Note: Register mapping is: E1=H2_MSB, E2=H1_LSB+H2_LSB, E3=H1_MSB
    calib_data.par_h1 = (coeff[27] << 4) | (coeff[26] & 0x0F);  // E3<<4 | E2<3:0>
    calib_data.par_h2 = (coeff[25] << 4) | (coeff[26] >> 4);    // E1<<4 | E2<7:4>
    calib_data.par_h3 = (int8_t)coeff[28];  // E4
    calib_data.par_h4 = (int8_t)coeff[29];  // E5
    calib_data.par_h5 = (int8_t)coeff[30];  // E6
    calib_data.par_h6 = coeff[31];          // E7
    calib_data.par_h7 = (int8_t)coeff[32];  // E8
    
    // Debug calibration values
    ESP_LOGI(TAG, "🔍 Calibration values:");
    ESP_LOGI(TAG, "   T1: %u, T2: %d, T3: %d", calib_data.par_t1, calib_data.par_t2, calib_data.par_t3);
    ESP_LOGI(TAG, "   P1: %u, P2: %d, P3: %d, P4: %d, P5: %d", 
             calib_data.par_p1, calib_data.par_p2, calib_data.par_p3, calib_data.par_p4, calib_data.par_p5);
    ESP_LOGI(TAG, "   H1: %u, H2: %u, H3: %d, H4: %d, H5: %d", 
             calib_data.par_h1, calib_data.par_h2, calib_data.par_h3, calib_data.par_h4, calib_data.par_h5);
    
    ESP_LOGI(TAG, "✅ Calibration data loaded");
    return ESP_OK;
}

// Temperature compensation
static float bme680_compensate_temperature(uint32_t temp_adc)
{
    int64_t var1, var2, var3;
    float temp_comp;
    
    var1 = (temp_adc >> 3) - ((int32_t)calib_data.par_t1 << 1);
    var2 = (var1 * (int32_t)calib_data.par_t2) >> 11;
    var3 = ((var1 >> 1) * (var1 >> 1)) >> 12;
    var3 = ((var3) * ((int32_t)calib_data.par_t3 << 4)) >> 14;
    
    calib_data.t_fine = var2 + var3;
    temp_comp = (((calib_data.t_fine * 5) + 128) >> 8);
    
    return temp_comp / 100.0f;
}

// Pressure compensation according to datasheet section 3.3.2 (Integer version)
static float bme680_compensate_pressure(uint32_t press_adc)
{
    int32_t var1, var2, var3, pressure_comp;
    
    var1 = ((int32_t)calib_data.t_fine >> 1) - 64000;
    var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)calib_data.par_p6) >> 2;
    var2 = var2 + ((var1 * (int32_t)calib_data.par_p5) << 1);
    var2 = (var2 >> 2) + ((int32_t)calib_data.par_p4 << 16);
    var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * ((int32_t)calib_data.par_p3 << 5)) >> 3) + 
           (((int32_t)calib_data.par_p2 * var1) >> 1);
    var1 = var1 >> 18;
    var1 = ((32768 + var1) * (int32_t)calib_data.par_p1) >> 15;
    
    pressure_comp = 1048576 - press_adc;
    pressure_comp = (int32_t)((pressure_comp - (var2 >> 12)) * ((uint32_t)3125));
    
    if (pressure_comp >= (1 << 30)) {
        pressure_comp = ((pressure_comp / (uint32_t)var1) << 1);
    } else {
        pressure_comp = ((pressure_comp << 1) / (uint32_t)var1);
    }
    
    var1 = ((int32_t)calib_data.par_p9 * (int32_t)(((pressure_comp >> 3) * (pressure_comp >> 3)) >> 13)) >> 12;
    var2 = ((int32_t)(pressure_comp >> 2) * (int32_t)calib_data.par_p8) >> 13;
    var3 = ((int32_t)(pressure_comp >> 8) * (int32_t)(pressure_comp >> 8) * 
           (int32_t)(pressure_comp >> 8) * (int32_t)calib_data.par_p10) >> 17;
    
    pressure_comp = (int32_t)(pressure_comp) + ((var1 + var2 + var3 + ((int32_t)calib_data.par_p7 << 7)) >> 4);
    
    return (float)pressure_comp; // Return Pa, will be converted to hPa in calling function
}

// Humidity compensation according to datasheet section 3.3.3 (Integer version)
static float bme680_compensate_humidity(uint16_t hum_adc)
{
    int32_t var1, var2, var3, var4, var5, var6, temp_scaled, calc_hum;
    
    temp_scaled = (int32_t)((calib_data.t_fine * 5) + 128) >> 8;
    var1 = (int32_t)hum_adc - (int32_t)((int32_t)calib_data.par_h1 << 4) - 
           (((temp_scaled * (int32_t)calib_data.par_h3) / ((int32_t)100)) >> 1);
    var2 = ((int32_t)calib_data.par_h2 * (((temp_scaled * (int32_t)calib_data.par_h4) / ((int32_t)100)) + 
           (((temp_scaled * ((temp_scaled * (int32_t)calib_data.par_h5) / ((int32_t)100))) >> 6) / ((int32_t)100)) + 
           ((int32_t)(1 << 14)))) >> 10;
    var3 = var1 * var2;
    var4 = (((int32_t)calib_data.par_h6 << 7) + 
           ((temp_scaled * (int32_t)calib_data.par_h7) / ((int32_t)100))) >> 4;
    var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
    var6 = (var4 * var5) >> 1;
    calc_hum = (((var3 + var6) >> 10) * ((int32_t)1000)) >> 12;
    
    if (calc_hum > 100000) {
        calc_hum = 100000;
    } else if (calc_hum < 0) {
        calc_hum = 0;
    }
    
    return calc_hum / 1000.0f;
}

// Gas resistance compensation
static float bme680_compensate_gas(uint16_t gas_adc, uint8_t gas_range)
{
    const float lookup_table[16] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.99f, 1.0f, 0.992f,
        1.0f, 1.0f, 0.998f, 0.995f, 1.0f, 0.99f, 1.0f, 1.0f
    };
    
    if (gas_range >= 16) gas_range = 15;
    
    float var1 = 1340.0f + (5.0f * 0.0f); // range_sw_err assumed 0
    float gas_res = var1 * (1 << gas_range) / (gas_adc - 512.0f + var1);
    
    return gas_res;
}

esp_err_t bme680_init(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "🌡️ Initializing BME680 sensor over SPI...");
    
    // SPI bus configuration
    spi_bus_config_t buscfg = {
        .mosi_io_num = BME680_MOSI_PIN,
        .miso_io_num = BME680_MISO_PIN,
        .sclk_io_num = BME680_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };
    
    // Initialize SPI bus
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // SPI device configuration
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 8,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 100000,  // 100kHz for BME680
        .spics_io_num = BME680_CS_PIN,
        .flags = 0,
        .queue_size = 7,
        .pre_cb = NULL,
        .post_cb = NULL
    };
    
    // Add device to SPI bus
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &bme680_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "📍 SPI Configuration:");
    ESP_LOGI(TAG, "   MOSI: GPIO%d (D10) → SDA", BME680_MOSI_PIN);
    ESP_LOGI(TAG, "   MISO: GPIO%d (D9)  → SDO", BME680_MISO_PIN);
    ESP_LOGI(TAG, "   SCK:  GPIO%d (D8)  → SCL", BME680_SCK_PIN);
    ESP_LOGI(TAG, "   CS:   GPIO%d (D0)  → CS", BME680_CS_PIN);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Check chip ID with detailed debugging
    ESP_LOGI(TAG, "🔍 Attempting to read chip ID...");
    uint8_t chip_id;
    
    // Try multiple reads to debug communication
    for (int attempts = 0; attempts < 3; attempts++) {
        ret = bme680_spi_read_reg(BME680_CHIP_ID_REG, &chip_id, 1);
        ESP_LOGI(TAG, "   Attempt %d: ret=%s, chip_id=0x%02X", 
                 attempts + 1, esp_err_to_name(ret), chip_id);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "💡 Check SPI connections:");
        ESP_LOGE(TAG, "   MOSI/SDA → GPIO10 (D10)");
        ESP_LOGE(TAG, "   MISO/SDO → GPIO9 (D9)");
        ESP_LOGE(TAG, "   SCK/SCL  → GPIO8 (D8)");
        ESP_LOGE(TAG, "   CS       → GPIO20 (D0)");
        ESP_LOGE(TAG, "   VCC      → 3.3V");
        ESP_LOGE(TAG, "   GND      → GND");
        ESP_LOGE(TAG, "💡 Make sure BME680 is powered and CS is connected");
        return ret;
    }
    
    if (chip_id != BME680_CHIP_ID) {
        ESP_LOGE(TAG, "❌ Invalid chip ID: 0x%02X (expected 0x%02X)", chip_id, BME680_CHIP_ID);
        ESP_LOGE(TAG, "💡 Make sure BME680 is in SPI mode (not I2C)");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "✅ BME680 chip detected (ID: 0x%02X)", chip_id);
    
    // Soft reset
    ret = bme680_spi_write_reg(BME680_RESET_REG, 0xB6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset sensor");
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Read calibration data
    ret = bme680_read_calibration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data");
        return ret;
    }
    
    // Wait for sensor to be ready after reset
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Configure according to datasheet section 3.2.1 Quick start
    // 1. Set humidity oversampling to 1x (recommended first step)
    ret = bme680_spi_write_reg(BME680_CTRL_HUM_REG, BME680_OS_HUM_X1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set humidity oversampling");
        return ret;
    }
    
    // 2-3. Set temperature (2x) and pressure (16x) oversampling in sleep mode
    uint8_t ctrl_meas = (BME680_OS_TEMP_X2 << 5) | (BME680_OS_PRESS_X16 << 2) | 0x00; // Sleep mode
    ret = bme680_spi_write_reg(BME680_CTRL_MEAS_REG, ctrl_meas);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set measurement control");
        return ret;
    }
    
    // Configure IIR filter (disabled for now to avoid complexity)
    ret = bme680_spi_write_reg(BME680_CONFIG_REG, 0x00); // Filter off
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set filter");
        return ret;
    }
    
    ESP_LOGI(TAG, "🔥 BME680 initialized successfully!");
    ESP_LOGI(TAG, "📊 Sensors: Temperature, Pressure, Humidity, Gas");
    
    return ESP_OK;
}

esp_err_t bme680_read_data(bme680_data_t *data)
{
    if (data == NULL || bme680_spi_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize data structure
    memset(data, 0, sizeof(bme680_data_t));
    
    // Set humidity oversampling first (required before ctrl_meas)
    esp_err_t ret = bme680_spi_write_reg(BME680_CTRL_HUM_REG, BME680_OS_HUM_X1);
    if (ret != ESP_OK) return ret;
    
    // Trigger forced mode measurement (simplified - no gas sensor)
    uint8_t ctrl_meas = (BME680_OS_TEMP_X2 << 5) | (BME680_OS_PRESS_X4 << 2) | 0x01; // Forced mode
    ret = bme680_spi_write_reg(BME680_CTRL_MEAS_REG, ctrl_meas);
    if (ret != ESP_OK) return ret;
    
    // Wait for measurement completion (reduced time)
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Read raw data (temperature, pressure, humidity)
    uint8_t raw_data[8];
    ret = bme680_spi_read_reg(BME680_PRESS_MSB_REG, raw_data, 8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read measurement data");
        return ret;
    }
    
    // Parse raw data with proper bit shifts according to datasheet
    uint32_t press_adc = ((uint32_t)raw_data[0] << 12) | ((uint32_t)raw_data[1] << 4) | ((uint32_t)raw_data[2] >> 4);
    uint32_t temp_adc = ((uint32_t)raw_data[3] << 12) | ((uint32_t)raw_data[4] << 4) | ((uint32_t)raw_data[5] >> 4);
    uint16_t hum_adc = ((uint16_t)raw_data[6] << 8) | raw_data[7];
    
    // Debug raw values with more detail
    ESP_LOGI(TAG, "🔍 Raw ADC values:");
    ESP_LOGI(TAG, "   Temp ADC: 0x%06lX (%lu decimal)", temp_adc, temp_adc);
    ESP_LOGI(TAG, "   Press ADC: 0x%06lX (%lu decimal)", press_adc, press_adc);
    ESP_LOGI(TAG, "   Hum ADC: 0x%04X (%u decimal)", hum_adc, hum_adc);
    ESP_LOGI(TAG, "   Raw bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
             raw_data[0], raw_data[1], raw_data[2], raw_data[3],
             raw_data[4], raw_data[5], raw_data[6], raw_data[7]);
    
    // Compensate measurements - check for valid data first
    if (temp_adc != 0x80000 && temp_adc != 0) {
        data->temperature = bme680_compensate_temperature(temp_adc);
        data->valid = true;
    } else {
        ESP_LOGW(TAG, "Invalid temperature reading: 0x%lX", temp_adc);
        data->temperature = 0.0f;
        data->valid = false;
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    if (press_adc != 0x80000 && press_adc != 0) {
        float pressure_pa = bme680_compensate_pressure(press_adc);
        data->pressure = pressure_pa / 100.0f; // Convert Pa to hPa
        ESP_LOGI(TAG, "   Pressure: %.2f Pa -> %.2f hPa", pressure_pa, data->pressure);
    } else {
        ESP_LOGW(TAG, "Invalid pressure reading: 0x%lX", press_adc);
        data->pressure = 0.0f;
    }
    
    if (hum_adc != 0x8000 && hum_adc != 0) {
        data->humidity = bme680_compensate_humidity(hum_adc);
    } else {
        ESP_LOGW(TAG, "Invalid humidity reading: 0x%X", hum_adc);
        data->humidity = 0.0f;
    }
    
    // Gas sensor disabled for now
    data->gas_resistance = 0.0f;
    data->gas_valid = 0;
    
    return ESP_OK;
}

void bme680_cleanup(void)
{
    if (bme680_spi_handle != NULL) {
        spi_bus_remove_device(bme680_spi_handle);
        bme680_spi_handle = NULL;
    }
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "BME680 SPI cleanup complete");
}
