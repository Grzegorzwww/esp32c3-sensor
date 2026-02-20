#include "current_sensor.h"
#include "flash_manager.h"
#include "communication.h"

static QueueHandle_t uart_queue;
static uint32_t last_total_consumption = 0;
static uint32_t kwh_to_pln = 0;

static iec62056_data_t meter_data = {0};


void init_current_sensor()
{
    init_uart();
    
    // Daj czas na uruchomienie UART task
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // NIE wywołuj read_data_from_IEC1107() przy starcie jeśli licznik może być niepodłączony
    // Ta funkcja będzie wywołana później przez create_and_publish_raport()
    
    last_total_consumption = 0;
    kwh_to_pln = 0;
    
    ESP_LOGI("CURRENT_SENSOR", "✅ Current sensor initialized (ready for meter reading)");
}

void init_uart()
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, GPIO_NUM_1, GPIO_NUM_3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, 1024 * 2, 0, 0, NULL, 0);

}

void write_uart(const char* data)
{
    uart_write_bytes(UART_NUM_0, data, strlen(data));
}



bool parse_mqtt_message(const char* topic, const char* data)
{

    char buffer[32] = {0};

    if (strcmp(topic, MQTT_TOPIC_CURRENT_SET_PRICE_ONE_KWH) == 0) {
        if (data == NULL || strlen(data) == 0) {
            ESP_LOGW("MQTT", "Received empty price value");
            return false;
        }
        if (atoi(data) == 0 && strcmp(data, "0") != 0) {
            ESP_LOGW("MQTT", "Received invalid price value: %s", data);
            return false;
        }
        kwh_to_pln = atoi(data);
        return true;
    }else if (strcmp(topic, MQTT_TOPIC_CURRENT_SET_TOTAL) == 0) {
            int total_consumption_to_save = atoi(data);
            if (total_consumption_to_save == 0 && strcmp(data, "0") != 0) {
                ESP_LOGW("MQTT", "Received invalid total consumption value: %d", total_consumption_to_save);
                return false;
            } else {
                // Zapisz poprawną wartość do NVS
                snprintf(buffer, sizeof(buffer), "%d", total_consumption_to_save);
                flash_manager_save_string(FLASH_TOTAL_CONSUMPTION, buffer);
                ESP_LOGI("MQTT", "Total consumption updated to: %s", buffer);
            }

            return true;
    }else if (strcmp(topic, MQTT_TOPIC_CURRENT_GET_TOTAL) == 0) {
        if(read_data_from_IEC1107()){
            char total_consumption_str[32];
            snprintf(total_consumption_str, sizeof(total_consumption_str), "%ld", meter_data.kwh_import_total);
            communication_publish_data(MQTT_TOPIC_CURRENT_GET_TOTAL, total_consumption_str);
        } else {
            ESP_LOGE("MQTT", "Failed to read current from meter");

        }

    }

    return false;
}


bool read_data_from_IEC1107()
{
    // Sprawdź czy UART jest zainicjalizowany
    if (uart_queue == NULL) {
        ESP_LOGE("IEC62056", "❌ UART queue not initialized!");
        return false;
    }
    
    // 62056-21
    char serial_number[16] = {0};
    char ack_msg[8];

    uint8_t rx_buffer[2048];
    int rx_len = 0;

    ESP_LOGI("IEC62056", "📡 Starting meter readout...");
    write_uart(IEC_REQUEST_INIT);

    vTaskDelay(pdMS_TO_TICKS(500));  // Czekaj na odpowiedź

    rx_len = uart_read_bytes(UART_NUM_0, rx_buffer, sizeof(rx_buffer) - 1, 
                        pdMS_TO_TICKS(2000));

     if (rx_len <= 0) {
        ESP_LOGE("IEC62056", "   No response from meter");
        return false;
    }
    
    rx_buffer[rx_len] = '\0';
    ESP_LOGI("IEC62056", "   Received (%d bytes): %s", rx_len, rx_buffer);

    if (rx_buffer[0] != '/') {
        ESP_LOGE("IEC62056", "Invalid identification response");
        return false;
    }


   
    sscanf((char*)rx_buffer, "/SAT6EM720%s", serial_number);
    ESP_LOGI("IEC62056", "   Device: EM720, Serial: %s", serial_number);

    

    vTaskDelay(pdMS_TO_TICKS(300));


    //  Wyślij ACK + prędkość transmisji

    snprintf(ack_msg, sizeof(ack_msg), "%s%s\r\n", IEC_ACK, IEC_BAUD_19200);
    write_uart(ack_msg);
    ESP_LOGI("IEC62056", "   Sent: ACK 0 6 0 (19200 Bd, no switching)");
    vTaskDelay(pdMS_TO_TICKS(500));


    uart_flush_input(UART_NUM_0);

    uint32_t x_len = uart_read_bytes(UART_NUM_0, rx_buffer, sizeof(rx_buffer) - 1,
                             pdMS_TO_TICKS(5000));
   if (x_len > 0) {
       ESP_LOGI("IEC62056", "   Received (%d bytes): %s", x_len, rx_buffer);
   } else {
       ESP_LOGE("IEC62056", "   No response from meter");
   }

   if (rx_buffer[0] != IEC_STX) {
        ESP_LOGW("IEC62056", " Data doesn't start with STX");
    }

    if(parse_data_from_meter(rx_buffer, rx_len, &meter_data)){
        ESP_LOGI("IEC62056", "   Data parsed successfully");
        return true;

    } else {
        ESP_LOGE("IEC62056", "   Failed to parse data");
        return false;
    }
    return false;
}

bool parse_data_from_meter(const uint8_t* data, int len, iec62056_data_t* out_data)
{
    int line_count = 0;


    char *line_start = strtok(data, "\r\n");
    while (line_start != NULL && line_count < 200) {
        line_count++;
        
        // Szukaj kodów OBIS
        if (strstr(line_start, "1.8.0(") != NULL) {
            // kWh import total
            sscanf(line_start, "1.8.0(%ld*kWh)", &meter_data.kwh_import_total);
            ESP_LOGI("IEC62056", "   kWh Import: %ld kWh", meter_data.kwh_import_total);
        }
        else if (strstr(line_start, "2.8.0(") != NULL) {
            // kWh export total
            sscanf(line_start, "2.8.0(%ld*kWh)", &meter_data.kwh_export_total);
            ESP_LOGI("IEC62056", "   kWh Export: %ld kWh", meter_data.kwh_export_total);
        }
        else if (strstr(line_start, "3.8.0(") != NULL) {
            // kvarh import
            sscanf(line_start, "3.8.0(%ld*kvarh)", &meter_data.kvarh_import_total);
            ESP_LOGI("IEC62056", "   kvarh Import: %ld kvarh", meter_data.kvarh_import_total);
        }
        else if (strstr(line_start, "4.8.0(") != NULL) {
            // kvarh export
            sscanf(line_start, "4.8.0(%ld*kvarh)", &meter_data.kvarh_export_total);
            ESP_LOGI("IEC62056", "   kvarh Export: %ld kvarh", meter_data.kvarh_export_total);
        }
        else if (strstr(line_start, "0.9.1(") != NULL) {
            // Czas
            char time_str[16];
            sscanf(line_start, "0.9.1(%15[^)])", time_str);
            ESP_LOGI("IEC62056", "   Time: %s", time_str);
        }
        else if (strstr(line_start, "0.9.2(") != NULL) {
            // Data
            char date_str[16];
            sscanf(line_start, "0.9.2(%15[^)])", date_str);
            ESP_LOGI("IEC62056", "    Date: %s", date_str);
        }
        
        line_start = strtok(NULL, "\r\n");
    }


    ESP_LOGI("IEC62056", " Import: %d kWh", meter_data.kwh_import_total);
    ESP_LOGI("IEC62056", " kvarh Import: %d kvarh", meter_data.kvarh_import_total);


    meter_data.valid = true;

    flash_manager_write("kwh_import", &meter_data.kwh_import_total, sizeof(float));


    return true;



}
bool create_and_publish_raport()
{
    read_data_from_IEC1107();

    char msg[50];
    msg[0] = '\0';


    sprintf(msg, "%ld", meter_data.kwh_import_total);
    communication_publish_data(MQTT_TOPIC_CURRENT_TOTAL, msg);

     if( last_total_consumption == 0){
        last_total_consumption = meter_data.kwh_import_total;
    }else{
        uint32_t consumption_24h = meter_data.kwh_import_total - last_total_consumption;
        sprintf(msg, "%ld", consumption_24h);
        communication_publish_data(MQTT_TOPIC_CURRENT_24H, msg);
        last_total_consumption = meter_data.kwh_import_total;

        sprintf(msg, "%.1f", (float)(consumption_24h * (kwh_to_pln/  100)));
        communication_publish_data(MQTT_TOPIC_CURRENT_24H_PRICE, msg);
    }
    return true;
}


bool current_sensor_analyze_data(bool (*is_time)(void))
{
    if(is_time()){
        create_and_publish_raport();
        return true;
    }
    return false;
}




