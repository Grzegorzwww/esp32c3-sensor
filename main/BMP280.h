#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/spi_master.h"

// BMP280 Register addresses
#define BMP280_REG_TEMP_XLSB    0xFC
#define BMP280_REG_TEMP_LSB     0xFB
#define BMP280_REG_TEMP_MSB     0xFA
#define BMP280_REG_PRESS_XLSB   0xF9
#define BMP280_REG_PRESS_LSB    0xF8
#define BMP280_REG_PRESS_MSB    0xF7
#define BMP280_REG_CONFIG       0xF5
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_STATUS       0xF3
#define BMP280_REG_RESET        0xE0
#define BMP280_REG_ID           0xD0

// Calibration registers
#define BMP280_REG_DIG_T1       0x88
#define BMP280_REG_DIG_T2       0x8A
#define BMP280_REG_DIG_T3       0x8C
#define BMP280_REG_DIG_P1       0x8E
#define BMP280_REG_DIG_P2       0x90
#define BMP280_REG_DIG_P3       0x92
#define BMP280_REG_DIG_P4       0x94
#define BMP280_REG_DIG_P5       0x96
#define BMP280_REG_DIG_P6       0x98
#define BMP280_REG_DIG_P7       0x9A
#define BMP280_REG_DIG_P8       0x9C
#define BMP280_REG_DIG_P9       0x9E

// BMP280 chip ID
#define BMP280_CHIP_ID          0x58

// SPI Configuration for XIAO ESP32-C3 - Correct pinout
#define BMP280_SPI_HOST         SPI2_HOST
#define BMP280_PIN_MISO         9   // GPIO9 (D9) - SDD (Serial Data Do/MISO)
#define BMP280_PIN_MOSI         10  // GPIO10 (D10) - SDA (Serial Data/MOSI)  
#define BMP280_PIN_CLK          8   // GPIO8 (D8) - SCL (Serial Clock/SCK)
#define BMP280_PIN_CS           2   // GPIO2 (D0) - CSB (Chip Select/CS)

// Power modes
typedef enum {
    BMP280_MODE_SLEEP  = 0x00,
    BMP280_MODE_FORCED = 0x01,
    BMP280_MODE_NORMAL = 0x03
} bmp280_mode_t;

// Oversampling settings
typedef enum {
    BMP280_OVERSAMPLING_SKIP = 0x00,
    BMP280_OVERSAMPLING_1X   = 0x01,
    BMP280_OVERSAMPLING_2X   = 0x02,
    BMP280_OVERSAMPLING_4X   = 0x03,
    BMP280_OVERSAMPLING_8X   = 0x04,
    BMP280_OVERSAMPLING_16X  = 0x05
} bmp280_oversampling_t;

// Filter settings
typedef enum {
    BMP280_FILTER_OFF = 0x00,
    BMP280_FILTER_2   = 0x01,
    BMP280_FILTER_4   = 0x02,
    BMP280_FILTER_8   = 0x03,
    BMP280_FILTER_16  = 0x04
} bmp280_filter_t;

// Standby time settings
typedef enum {
    BMP280_STANDBY_0_5_MS   = 0x00,
    BMP280_STANDBY_62_5_MS  = 0x01,
    BMP280_STANDBY_125_MS   = 0x02,
    BMP280_STANDBY_250_MS   = 0x03,
    BMP280_STANDBY_500_MS   = 0x04,
    BMP280_STANDBY_1000_MS  = 0x05,
    BMP280_STANDBY_2000_MS  = 0x06,
    BMP280_STANDBY_4000_MS  = 0x07
} bmp280_standby_t;

// Calibration data structure
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp280_calib_data_t;

// BMP280 configuration structure
typedef struct {
    bmp280_mode_t mode;
    bmp280_oversampling_t temp_oversampling;
    bmp280_oversampling_t press_oversampling;
    bmp280_filter_t filter;
    bmp280_standby_t standby_time;
} bmp280_config_t;

// BMP280 measurement data structure
typedef struct {
    float temperature;  // Temperature in degrees Celsius
    float pressure;     // Pressure in hPa (hectopascals)
    float altitude;     // Altitude in meters (calculated from pressure)
} bmp280_data_t;

// Function declarations

/**
 * @brief Initialize BMP280 sensor with SPI interface
 * @param config Pointer to BMP280 configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_init(const bmp280_config_t *config);

/**
 * @brief Deinitialize BMP280 sensor and free resources
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_deinit(void);

/**
 * @brief Read temperature and pressure from BMP280
 * @param data Pointer to structure where measurements will be stored
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_read_measurements(bmp280_data_t *data);

/**
 * @brief Read only temperature from BMP280 (faster)
 * @param temperature Pointer to store temperature in degrees Celsius
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_read_temperature(float *temperature);

/**
 * @brief Read only pressure from BMP280
 * @param pressure Pointer to store pressure in hPa
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_read_pressure(float *pressure);

/**
 * @brief Check if BMP280 is connected and responding
 * @return true if sensor is connected, false otherwise
 */
bool bmp280_is_connected(void);

/**
 * @brief Get default configuration for BMP280
 * @param config Pointer to configuration structure to fill
 */
void bmp280_get_default_config(bmp280_config_t *config);

/**
 * @brief Calculate altitude from pressure (using standard atmosphere)
 * @param pressure Pressure in hPa
 * @param sea_level_pressure Sea level pressure in hPa (default: 1013.25)
 * @return Altitude in meters
 */
float bmp280_calculate_altitude(float pressure, float sea_level_pressure);

#endif // BMP280_H
