#ifndef BME680_H
#define BME680_H

#include "esp_err.h"
#include "bme68x.h"
#include <stdbool.h>

// Wrapper structure for compatibility
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

#endif // BME680_H
