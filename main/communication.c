#include "communication.h"
#include "config.h"  // Konfiguracja użytkownika
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"


#ifdef defined(PAULINA)
    // 💝 Konfiguracja Pauliny
    // #define WIFI_SSID "Dom"
    // #define WIFI_PASS "paula1234"
    #define WIFI_SSID "FunBox2-9877"
    #define WIFI_PASS "22446688"
    #define MQTT_BROKER_URI "mqtts://a51fd01c7c0b4e2b881c011bfbc0d781.s2.eu.hivemq.cloud:8883"
    #define MQTT_USERNAME "paulina"
    #define MQTT_PASSWORD "Metypret69"
    #define MQTT_CLIENT_ID "esp32c3_sensor_paulina"
#elif defined(BOBIK)
    // 🏠 Konfiguracja domyślna (Twoja)
    #define WIFI_SSID "FunBox2-C259"  // POPRAWIONE z C256 na C259
    // #define WIFI_SSID "iPhone"
    // #define WIFI_PASS "bobik111"
    #define WIFI_PASS "22446688"
    #define MQTT_BROKER_URI "mqtts://3a740c0f200c45698faee4ba7744b88c.s2.eu.hivemq.cloud:8883"
    #define MQTT_USERNAME "polnocna27"
    #define MQTT_PASSWORD "Bobik111"
    #define MQTT_CLIENT_ID "esp32c3_sensor"
#elif defined(WESOLA)
    #define WIFI_SSID "TP-Link_7E81"
    #define WIFI_PASS "39693617"
    #define MQTT_BROKER_URI "mqtts://3a740c0f200c45698faee4ba7744b88c.s2.eu.hivemq.cloud:8883"
    #define MQTT_USERNAME "polnocna27"
    #define MQTT_PASSWORD "Bobik111"
    #define MQTT_CLIENT_ID "esp32c3_sensor_wesola"
#else 
    #error "Brak wybranej konfiguracji (zdefiniuj PAULINA/BOBIK/WESOLA)".
    #endif

// Wspólne ustawienia
#define WIFI_TIMEOUT_MS 10000
#define MQTT_PORT 8883
#define MQTT_TOPIC_STATUS "esp32c3/status"
#define MQTT_TOPIC_DATA "esp32c3/sensor_data"
#define MQTT_TOPIC_WIFI_QUALITY "esp32c3/wifi_quality"
#define MQTT_TOPIC_BOOT_COUNT "esp32c3/boot_count"
#define MQTT_TOPIC_BATTERY_VOLTAGE "esp32c3/battery_voltage"
#define MQTT_TOPIC_BATTERY_PERCENT "esp32c3/battery_percent"

// Zmienne globalne modułu
static const char *TAG = "COMMUNICATION";
static EventGroupHandle_t s_wifi_event_group;
static bool module_initialized = false;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;
static int wifi_rssi = -100;  // Siła sygnału WiFi
static bool connection_info_sent = false;  // Flaga czy wysłano już info o połączeniu

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

// Funkcja do przeliczania RSSI na jakość w procentach
static int rssi_to_quality_percent(int rssi)
{
    // RSSI w dBm -> jakość w %
    // -30 dBm (doskonała) = 100%
    // -50 dBm (bardzo dobra) = 80%
    // -70 dBm (dobra) = 60%
    // -80 dBm (średnia) = 40%
    // -90 dBm (słaba) = 20%
    // -100 dBm (bardzo słaba) = 0%
    
    if (rssi >= -30) return 100;
    if (rssi >= -50) return 80 + (rssi + 50) * 20 / 20;  // 80-100%
    if (rssi >= -70) return 60 + (rssi + 70) * 20 / 20;  // 60-80%
    if (rssi >= -80) return 40 + (rssi + 80) * 20 / 10;  // 40-60%
    if (rssi >= -90) return 20 + (rssi + 90) * 20 / 10;  // 20-40%
    if (rssi >= -100) return (rssi + 100) * 20 / 10;     // 0-20%
    return 0;
}

// Funkcja do wysyłania informacji o połączeniu (jednorazowo po połączeniu)
static void send_connection_info(void)
{
    if (connection_info_sent || !mqtt_connected) {
        return;
    }
    
    // Pobierz aktualną siłę sygnału WiFi
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        wifi_rssi = ap_info.rssi;
    }
    
    // Przelicz RSSI na jakość w procentach
    int quality_percent = rssi_to_quality_percent(wifi_rssi);
    
    // Pobierz numer uruchomienia z NVS
    nvs_handle_t nvs_handle;
    int32_t boot_count = 0;
    
    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        ret = nvs_get_i32(nvs_handle, "boot_count", &boot_count);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            boot_count = 0;  // Pierwsze uruchomienie
        }
        boot_count++;
        nvs_set_i32(nvs_handle, "boot_count", boot_count);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    // Wysłanie informacji o jakości WiFi
    char quality_str[16];
    snprintf(quality_str, sizeof(quality_str), "%d", quality_percent);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_WIFI_QUALITY, quality_str, 0, 1, 0);
    
    // Wysłanie numeru uruchomienia
    char boot_str[16];
    snprintf(boot_str, sizeof(boot_str), "%ld", boot_count);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_BOOT_COUNT, boot_str, 0, 1, 0);
    
    ESP_LOGI(TAG, "📊 Connection info sent:");
    ESP_LOGI(TAG, "   📶 WiFi Quality: %d%% (RSSI: %d dBm)", quality_percent, wifi_rssi);
    ESP_LOGI(TAG, "   🔄 Boot Count: %ld", boot_count);
    
    connection_info_sent = true;
}

// Handler zdarzeń WiFi z szczegółowym debugowaniem
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "📡 WiFi station started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "❌ WiFi disconnected! Reason: %d (%s)", disconnected->reason, 
                 disconnected->reason == 2 ? "AUTH_EXPIRE" :
                 disconnected->reason == 4 ? "PROBE_REQ_TIMEOUT" :
                 disconnected->reason == 8 ? "ASSOC_LEAVE" :
                 disconnected->reason == 15 ? "4WAY_HANDSHAKE_TIMEOUT" :
                 disconnected->reason == 201 ? "NO_AP_FOUND" :
                 disconnected->reason == 205 ? "AUTH_FAIL" : "OTHER");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "🔄 Attempting to reconnect...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "✅ WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// Handler zdarzeń MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "🔗 MQTT connected to broker");
        mqtt_connected = true;
        xEventGroupSetBits(s_wifi_event_group, MQTT_CONNECTED_BIT);
        
        // Publikuj status połączenia
        esp_mqtt_client_publish(event->client, MQTT_TOPIC_STATUS, "ESP32-C3 connected", 0, 1, 0);
        ESP_LOGI(TAG, "📢 Published status: connected");
        
        // Wyślij informacje o połączeniu (jednorazowo)
        send_connection_info();
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "❌ MQTT disconnected from broker");
        mqtt_connected = false;
        xEventGroupClearBits(s_wifi_event_group, MQTT_CONNECTED_BIT);
        break;
        
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "📥 MQTT subscribed, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "📤 MQTT unsubscribed, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "📨 MQTT published, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "📩 MQTT data received:");
        ESP_LOGI(TAG, "   Topic: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "   Data: %.*s", event->data_len, event->data);
        break;
        
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "❌ MQTT error event");
        break;
        
    default:
        ESP_LOGI(TAG, "🔄 MQTT event: %d", event_id);
        break;
    }
}

// Certyfikat SSL serwera (musisz dodać odpowiedni certyfikat dla HiveMQ)
extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");

// Inicjalizacja MQTT
static esp_err_t mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg;
    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));

    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;
    mqtt_cfg.broker.verification.certificate = (const char *)server_cert_pem_start;
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    mqtt_cfg.credentials.client_id = MQTT_CLIENT_ID;
    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.network.timeout_ms = 10000;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "❌ Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_LOGI(TAG, "✅ MQTT client initialized");
    return ESP_OK;
}

// Inicjalizacja WiFi
static esp_err_t wifi_init(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    return ESP_OK;
}

// Skanuje dostępne sieci WiFi i sprawdza siłę sygnału
static void scan_and_check_signal(void)
{
    ESP_LOGI(TAG, "🔍 Scanning for available networks...");
    ESP_LOGI(TAG, "📡 NOTE: External antenna should be connected to U.FL connector");
    ESP_LOGI(TAG, "🔧 Antenna status: Check if antenna is properly attached");
    
    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t*)WIFI_SSID,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 300,  // Zwiększone z 120 do 300ms
        .scan_time.active.max = 500,  // Zwiększone z 150 do 500ms
    };
    
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        ESP_LOGW(TAG, "❌ No access points found!");
        return;
    }
    
    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));
    
    ESP_LOGI(TAG, "📡 Found %d access points (with external antenna):", ap_count);
    bool target_found = false;
    
    // Pokaż wszystkie znalezione sieci z oceną sygnału
    for (int i = 0; i < ap_count; i++) {
        const char* signal_quality = 
            ap_list[i].rssi >= -50 ? "EXCELLENT" :
            ap_list[i].rssi >= -60 ? "VERY GOOD" :
            ap_list[i].rssi >= -70 ? "GOOD" :
            ap_list[i].rssi >= -80 ? "FAIR" :
            ap_list[i].rssi >= -90 ? "WEAK" : "VERY WEAK";
        
        const char* auth_type = 
            ap_list[i].authmode == WIFI_AUTH_OPEN ? "OPEN" :
            ap_list[i].authmode == WIFI_AUTH_WEP ? "WEP" :
            ap_list[i].authmode == WIFI_AUTH_WPA_PSK ? "WPA" :
            ap_list[i].authmode == WIFI_AUTH_WPA2_PSK ? "WPA2" :
            ap_list[i].authmode == WIFI_AUTH_WPA_WPA2_PSK ? "WPA/WPA2" : "OTHER";
        
        ESP_LOGI(TAG, "   [%d] '%s' | %d dBm (%s) | Ch:%d | %s", 
                 i, ap_list[i].ssid, ap_list[i].rssi, signal_quality, 
                 ap_list[i].primary, auth_type);
        
        if (strcmp((char*)ap_list[i].ssid, WIFI_SSID) == 0) {
            target_found = true;
            ESP_LOGI(TAG, "🎯 TARGET FOUND: '%s' | RSSI: %d dBm (%s)", 
                     WIFI_SSID, ap_list[i].rssi, signal_quality);
            
            if (ap_list[i].rssi < -80) {
                ESP_LOGW(TAG, "⚠️ STILL WEAK! External antenna helped but signal is still low");
                ESP_LOGW(TAG, "   Consider moving closer or using directional antenna");
            } else if (ap_list[i].rssi < -70) {
                ESP_LOGI(TAG, "✅ BETTER! External antenna improved signal (should work)");
            } else {
                ESP_LOGI(TAG, "🚀 EXCELLENT! External antenna provides strong signal");
            }
        }
    }
    
    if (!target_found) {
        ESP_LOGE(TAG, "❌ Target network '%s' NOT FOUND in scan results!", WIFI_SSID);
        ESP_LOGE(TAG, "   This suggests the ESP32-C3 is too far from the router!");
    }
    
    free(ap_list);
}

// Łączenie z WiFi
static bool wifi_connect(void)
{
    ESP_LOGI(TAG, "🌐 Connecting to WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, "🔑 Password length: %d characters", strlen(WIFI_PASS));
    
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);   
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,  // Skanuj wszystkie kanały, nie tylko szybkie
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,  // Połącz z najsilniejszym sygnałem
            .channel = 0,  // Auto-detect kanału
        },
    };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    
    ESP_LOGI(TAG, "🔍 Scan method: ALL_CHANNEL (not FAST) to find hidden/weak networks");
    
    ESP_LOGI(TAG, "🔐 WiFi config: SSID='%s', Pass='%s' (len:%d)", 
             wifi_config.sta.ssid, wifi_config.sta.password, strlen(WIFI_PASS));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ESP_LOGI(TAG, "📡 Starting WiFi...");
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Ustawienie maksymalnej mocy transmisji dla lepszego zasięgu
    ESP_LOGI(TAG, "⚡ Setting maximum TX power for better range");
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(78)); // 78 = ~19.5 dBm (max dla ESP32-C3)
    
    // Sprawdzenie aktualnej mocy transmisji
    int8_t current_power = 0;
    esp_wifi_get_max_tx_power(&current_power);
    ESP_LOGI(TAG, "📶 Current TX power: %d (0.25dBm units) = %.1f dBm", 
             current_power, current_power * 0.25f);
    
    // Opóźnienie aby moduł WiFi mógł się w pełni zainicjalizować
    ESP_LOGI(TAG, "⏱️ Waiting 2 seconds for WiFi hardware to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // DEBUG: Wykonaj proste skanowanie aby zobaczyć CO WIDZI ESP32
    ESP_LOGI(TAG, "🔍 DEBUG: Performing simple scan to see available networks...");
    wifi_scan_config_t debug_scan = {
        .ssid = NULL,  // Szukaj wszystkich sieci
        .bssid = NULL,
        .channel = 0,  // Wszystkie kanały
        .show_hidden = true,  // Pokaż ukryte sieci
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 0,
        .scan_time.active.max = 500,
    };
    
    esp_err_t scan_ret = esp_wifi_scan_start(&debug_scan, true);
    if (scan_ret == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        ESP_LOGI(TAG, "� Found %d access points total", ap_count);
        
        if (ap_count > 0) {
            wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (ap_list) {
                esp_wifi_scan_get_ap_records(&ap_count, ap_list);
                
                bool found_target = false;
                for (int i = 0; i < ap_count && i < 10; i++) {  // Pokaż max 10
                    ESP_LOGI(TAG, "   [%d] SSID:'%s' Ch:%d RSSI:%d Auth:%d", 
                             i, ap_list[i].ssid, ap_list[i].primary, 
                             ap_list[i].rssi, ap_list[i].authmode);
                    
                    if (strcmp((char*)ap_list[i].ssid, WIFI_SSID) == 0) {
                        found_target = true;
                        ESP_LOGI(TAG, "🎯 TARGET '%s' FOUND on channel %d with RSSI %d!", 
                                 WIFI_SSID, ap_list[i].primary, ap_list[i].rssi);
                    }
                }
                
                if (!found_target) {
                    ESP_LOGE(TAG, "❌ Target network '%s' NOT FOUND in scan!", WIFI_SSID);
                    ESP_LOGE(TAG, "   Check: 1) Router is 2.4GHz 2) SSID is correct 3) Router is ON");
                }
                
                free(ap_list);
            }
        } else {
            ESP_LOGE(TAG, "❌ NO NETWORKS FOUND AT ALL! Hardware issue or antenna problem?");
        }
    } else {
        ESP_LOGE(TAG, "❌ Scan failed: %s", esp_err_to_name(scan_ret));
    }
    
    ESP_LOGI(TAG, "🔌 Connecting to network...");
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "🔋 Setting WiFi power save mode (NONE like tutorial)");
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "⏳ Waiting for connection (timeout: %d ms)", WIFI_TIMEOUT_MS);
    int bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                   pdFALSE, pdTRUE, WIFI_TIMEOUT_MS / portTICK_PERIOD_MS);
    
    bool connected = (bits & WIFI_CONNECTED_BIT) != 0;
    
    if (connected) {
        ESP_LOGI(TAG, "🎉 WiFi connection successful!");
    } else {
        ESP_LOGE(TAG, "❌ WiFi connection timeout after %d ms", WIFI_TIMEOUT_MS);
    }
    
    return connected;
}

// Łączenie z MQTT broker
static bool mqtt_connect(void)
{
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "❌ MQTT client not initialized!");
        return false;
    }

    ESP_LOGI(TAG, "🔗 Connecting to MQTT broker: %s", MQTT_BROKER_URI);
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start MQTT client: %s", esp_err_to_name(ret));
        return false;
    }

    // Czekaj na połączenie z MQTT (timeout 10 sekund)
    ESP_LOGI(TAG, "⏳ Waiting for MQTT connection...");
    int bits = xEventGroupWaitBits(s_wifi_event_group, MQTT_CONNECTED_BIT,
                                   pdFALSE, pdTRUE, 10000 / portTICK_PERIOD_MS);
    
    bool connected = (bits & MQTT_CONNECTED_BIT) != 0;
    
    if (connected) {
        ESP_LOGI(TAG, "🎉 MQTT connection successful!");
    } else {
        ESP_LOGE(TAG, "❌ MQTT connection timeout");
    }
    
    return connected;
}

// === PUBLICZNE FUNKCJE === //

esp_err_t communication_init(void)
{
    if (module_initialized) {
        ESP_LOGW(TAG, "⚠️ Communication module already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🔧 Initializing communication module");

    // Wyświetl aktywną konfigurację
#ifdef PAULINA
    ESP_LOGI(TAG, "💝 Konfiguracja: PAULINA");
    ESP_LOGI(TAG, "📶 WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, "🔗 MQTT: paulina@hivemq.cloud");
#else
    ESP_LOGI(TAG, "🏠 Konfiguracja: DOMYŚLNA");
    ESP_LOGI(TAG, "📶 WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, "🔗 MQTT: polnocna27@hivemq.cloud");
#endif
    
    // Inicjalizacja NVS (wymagane dla WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS flash initialized");

    // Inicjalizacja WiFi
    ret = wifi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi initialization failed");
        return ret;
    }
    ESP_LOGI(TAG, "✅ WiFi initialized");

    // Inicjalizacja MQTT
    ret = mqtt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ MQTT initialization failed");
        return ret;
    }
    ESP_LOGI(TAG, "✅ MQTT initialized");

    module_initialized = true;
    ESP_LOGI(TAG, "✅ Communication module ready");
    return ESP_OK;
}

bool communication_connect_wifi(void)
{
    if (!module_initialized) {
        ESP_LOGE(TAG, "❌ Communication module not initialized!");
        return false;
    }

    return wifi_connect();
}

bool communication_connect_mqtt(void)
{
    if (!module_initialized) {
        ESP_LOGE(TAG, "❌ Communication module not initialized!");
        return false;
    }

    return mqtt_connect();
}

bool communication_publish_data(const char* topic, const char* data)
{
    if (!mqtt_connected || mqtt_client == NULL) {
        ESP_LOGE(TAG, "❌ MQTT not connected!");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, 0, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "❌ Failed to publish MQTT message");
        return false;
    }

    ESP_LOGI(TAG, "📤 Published to '%s': %s (msg_id=%d)", topic, data, msg_id);
    return true;
}

bool communication_is_mqtt_connected(void)
{
    return mqtt_connected;
}

void communication_reset_connection_info_flag(void)
{
    connection_info_sent = false;
}

void communication_cleanup(void)
{
    ESP_LOGI(TAG, "🧹 Cleaning up communication resources");

    if (module_initialized) {
        if (mqtt_client) {
            esp_mqtt_client_stop(mqtt_client);
            esp_mqtt_client_destroy(mqtt_client);
            mqtt_client = NULL;
        }
        esp_wifi_stop();
        esp_wifi_deinit();
        module_initialized = false;
        mqtt_connected = false;
    }

    ESP_LOGI(TAG, "✅ Communication cleanup completed");
}
