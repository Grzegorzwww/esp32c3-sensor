#ifndef BME680_WRAPPER_H
#define BME680_WRAPPER_H

#include <stdbool.h>
#include "esp_err.h"
#include "bme680.h"  // From the dedicated ESP-IDF library

// Wrapper structure for compatibility with our main.c
typedef struct {
    float temperature;    // Temperature in °C
    float pressure;      // Pressure in hPa
    float humidity;      // Humidity in %RH
    float gas_resistance; // Gas resistance in Ohms
    bool gas_valid;      // Gas measurement validity
    bool valid;          // Overall data validity
} bme680_data_t;

// Function declarations
esp_err_t bme680_init(void);
esp_err_t bme680_read_data(bme680_data_t *data);
void bme680_cleanup(void);

#endif // BME680_WRAPPER_H
