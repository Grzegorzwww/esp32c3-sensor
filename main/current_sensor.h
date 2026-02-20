#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "hal/uart_types.h"
#include "config.h"


#define IEC_REQUEST_INIT   "/?1!\r\n"         // Żądanie inicjalizacji
#define IEC_ACK            "\x06"             // ACK (0x06)
#define IEC_BAUD_19200     "060"              // 19200 Bd + bez przełączania
#define IEC_STX            0x02               // Start of Text
#define IEC_ETX            0x03               // End of Text
#define IEC_CR             '\r'
#define IEC_LF             '\n'


typedef struct {
    uint32_t kwh_import_total;       // 1.8.0 - kWh import (Q1+Q4)
    uint32_t kwh_export_total;       // 2.8.0 - kWh export (Q2+Q3)
    uint32_t kvarh_import_total;     // 3.8.0 - kvarh import (Q1+Q2)
    uint32_t kvarh_export_total;     // 4.8.0 - kvarh export (Q3+Q4)
    char timestamp[20];           // 0.9.1 + 0.9.2 - czas i data
    bool valid;
} iec62056_data_t;


void init_current_sensor();
void init_uart();
void write_uart(const char* data);


bool start_meassure();

bool parse_mqtt_message(const char* topic, const char* data);
bool parse_data_from_meter(const uint8_t* data, int len, iec62056_data_t* out_data);
bool read_data_from_IEC1107();
bool create_and_publish_raport();
bool current_sensor_analyze_data(bool (*is_time)(void));