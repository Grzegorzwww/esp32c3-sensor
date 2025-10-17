#ifndef BME680_H
#define BME680_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

// BME680 SPI Configuration - trying different CS pin
#define BME680_MOSI_PIN    GPIO_NUM_10   // D10 → SDA (MOSI)
#define BME680_MISO_PIN    GPIO_NUM_9    // D9  → SDO (MISO)  
#define BME680_SCK_PIN     GPIO_NUM_8    // D8  → SCL (SCLK)
#define BME680_CS_PIN      GPIO_NUM_20   // D0  → CS (trying different pin)

// BME680 Register addresses
#define BME680_CHIP_ID_REG     0xD0
#define BME680_RESET_REG       0xE0
#define BME680_STATUS_REG      0x73
#define BME680_MEAS_STATUS_REG 0x1D
#define BME680_CTRL_MEAS_REG   0x74
#define BME680_CTRL_HUM_REG    0x72
#define BME680_CTRL_GAS_1_REG  0x71
#define BME680_CONFIG_REG      0x75

// Gas sensor registers
#define BME680_RES_HEAT_0_REG  0x5A
#define BME680_GAS_WAIT_0_REG  0x64

// Gas sensor registers
#define BME680_RES_HEAT_0_REG  0x5A
#define BME680_GAS_WAIT_0_REG  0x64

// Data registers
#define BME680_TEMP_MSB_REG    0x22
#define BME680_TEMP_LSB_REG    0x23
#define BME680_TEMP_XLSB_REG   0x24
#define BME680_PRESS_MSB_REG   0x1F
#define BME680_PRESS_LSB_REG   0x20
#define BME680_PRESS_XLSB_REG  0x21
#define BME680_HUM_MSB_REG     0x25
#define BME680_HUM_LSB_REG     0x26
#define BME680_GAS_RES_MSB_REG 0x2A
#define BME680_GAS_RES_LSB_REG 0x2B

// Calibration registers according to datasheet Tables 11-13
#define BME680_COEFF_T1_LSB    0x89
#define BME680_COEFF_T1_MSB    0x8A
#define BME680_COEFF_T2_LSB    0x8B
#define BME680_COEFF_T2_MSB    0x8C
#define BME680_COEFF_T3_REG    0x8D

#define BME680_COEFF_P1_LSB    0x8E
#define BME680_COEFF_P1_MSB    0x8F
#define BME680_COEFF_P2_LSB    0x90
#define BME680_COEFF_P2_MSB    0x91
#define BME680_COEFF_P3_REG    0x92
#define BME680_COEFF_P4_LSB    0x94
#define BME680_COEFF_P4_MSB    0x95
#define BME680_COEFF_P5_LSB    0x96
#define BME680_COEFF_P5_MSB    0x97
#define BME680_COEFF_P6_REG    0x99
#define BME680_COEFF_P7_REG    0x98
#define BME680_COEFF_P8_LSB    0x9C
#define BME680_COEFF_P8_MSB    0x9D
#define BME680_COEFF_P9_LSB    0x9E
#define BME680_COEFF_P9_MSB    0x9F
#define BME680_COEFF_P10_REG   0xA0

// Humidity calibration registers (Table 13)
#define BME680_COEFF_H1_MSB    0xE3  // par_h1 MSB
#define BME680_COEFF_H1_LSB    0xE2  // par_h1 LSB <3:0>
#define BME680_COEFF_H2_MSB    0xE1  // par_h2 MSB
#define BME680_COEFF_H2_LSB    0xE2  // par_h2 LSB <7:4>
#define BME680_COEFF_H3_REG    0xE4
#define BME680_COEFF_H4_REG    0xE5
#define BME680_COEFF_H5_REG    0xE6
#define BME680_COEFF_H6_REG    0xE7
#define BME680_COEFF_H7_REG    0xE8

// BME680 chip ID
#define BME680_CHIP_ID         0x61

// Oversampling settings
#define BME680_OS_TEMP_SKIP    0x00
#define BME680_OS_TEMP_X1      0x01
#define BME680_OS_TEMP_X2      0x02
#define BME680_OS_TEMP_X4      0x03
#define BME680_OS_TEMP_X8      0x04
#define BME680_OS_TEMP_X16     0x05

#define BME680_OS_PRESS_SKIP   0x00
#define BME680_OS_PRESS_X1     0x01
#define BME680_OS_PRESS_X2     0x02
#define BME680_OS_PRESS_X4     0x03
#define BME680_OS_PRESS_X8     0x04
#define BME680_OS_PRESS_X16    0x05

#define BME680_OS_HUM_SKIP     0x00
#define BME680_OS_HUM_X1       0x01
#define BME680_OS_HUM_X2       0x02
#define BME680_OS_HUM_X4       0x03
#define BME680_OS_HUM_X8       0x04
#define BME680_OS_HUM_X16      0x05

// IIR Filter settings
#define BME680_FILTER_OFF      0x00
#define BME680_FILTER_COEFF_1  0x01
#define BME680_FILTER_COEFF_3  0x02
#define BME680_FILTER_COEFF_7  0x03
#define BME680_FILTER_COEFF_15 0x04
#define BME680_FILTER_COEFF_31 0x05
#define BME680_FILTER_COEFF_63 0x06
#define BME680_FILTER_COEFF_127 0x07

// Gas sensor settings
#define BME680_GAS_HEAT_DUR    150  // 150ms
#define BME680_GAS_HEAT_TEMP   320  // 320°C

// BME680 measurement data structure
typedef struct {
    float temperature;    // °C
    float pressure;      // hPa
    float humidity;      // %RH
    float gas_resistance; // Ohms
    uint8_t gas_valid;   // Gas measurement valid flag
    bool valid;          // Overall measurement valid
} bme680_data_t;

// BME680 calibration data structure
typedef struct {
    // Temperature calibration
    uint16_t par_t1;
    int16_t par_t2;
    int8_t par_t3;
    
    // Pressure calibration
    uint16_t par_p1;
    int16_t par_p2;
    int8_t par_p3;
    int16_t par_p4;
    int16_t par_p5;
    int8_t par_p6;
    int8_t par_p7;
    int16_t par_p8;
    int16_t par_p9;
    uint8_t par_p10;
    
    // Humidity calibration
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t par_h3;
    int8_t par_h4;
    int8_t par_h5;
    uint8_t par_h6;
    int8_t par_h7;
    
    // Gas sensor calibration
    int8_t par_gh1;
    int16_t par_gh2;
    int8_t par_gh3;
    
    int32_t t_fine;  // Fine temperature for pressure calculation
} bme680_calib_data_t;

/**
 * @brief Initialize BME680 sensor
 * @return ESP_OK on success
 */
esp_err_t bme680_init(void);

/**
 * @brief Read all sensor data from BME680
 * @param data Pointer to store measurement data
 * @return ESP_OK on success
 */
esp_err_t bme680_read_data(bme680_data_t *data);

/**
 * @brief Cleanup BME680 resources
 */
void bme680_cleanup(void);

#endif // BME680_H
