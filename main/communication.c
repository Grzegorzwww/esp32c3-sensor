

#include "communication.h"
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
#include "esp_sntp.h"
#include "esp_pm.h"
#include "current_sensor.h"
#include "flash_manager.h"
#include "config.h" // Konfiguracja użytkownika

#define LOG_LOCAL_LEVEL ESP_LOG_NONE

#ifdef PAULINA
// 💝 Konfiguracja Pauliny
#define WIFI_SSID "Dom"
#define WIFI_PASS "paula1234"
// #define WIFI_SSID "FunBox2-9877"
// #define WIFI_PASS "22446688"
#define MQTT_BROKER_URI "mqtts://a51fd01c7c0b4e2b881c011bfbc0d781.s2.eu.hivemq.cloud:8883"
#define MQTT_USERNAME "paulina"
#define MQTT_PASSWORD "Metypret69"
#define MQTT_CLIENT_ID "esp32c3_sensor_paulina"
#elif BOBIK
// 🏠 Konfiguracja domyślna (Twoja)
#define WIFI_SSID "FunBox2-C259"
#define WIFI_PASS "22446688"
#define MQTT_BROKER_URI "mqtts://3a740c0f200c45698faee4ba7744b88c.s2.eu.hivemq.cloud:8883"
#define MQTT_USERNAME "polnocna27"
#define MQTT_PASSWORD "Bobik111"
#define MQTT_CLIENT_ID "esp32c3_sensor"
#elif WESOLA
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
static int wifi_rssi = -100;              // Siła sygnału WiFi
static bool connection_info_sent = false; // Flaga czy wysłano już info o połączeniu

wifi_connection_mode_t wifi_mode = WIFI_NOT_CONFIGURED;

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

static parse_mqtt_data_callback_t parse_mqtt_data_callback = NULL;
// Funkcja do przeliczania RSSI na jakość w procentach
static int rssi_to_quality_percent(int rssi)
{

    if (rssi >= -30)
        return 100;
    if (rssi >= -50)
        return 80 + (rssi + 50) * 20 / 20; // 80-100%
    if (rssi >= -70)
        return 60 + (rssi + 70) * 20 / 20; // 60-80%
    if (rssi >= -80)
        return 40 + (rssi + 80) * 20 / 10; // 40-60%
    if (rssi >= -90)
        return 20 + (rssi + 90) * 20 / 10; // 20-40%
    if (rssi >= -100)
        return (rssi + 100) * 20 / 10; // 0-20%
    return 0;
}

// Funkcja do wysyłania informacji o połączeniu (jednorazowo po połączeniu)
static void send_connection_info(void)
{
    if (connection_info_sent || !mqtt_connected)
    {
        return;
    }

    // Pobierz aktualną siłę sygnału WiFi
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK)
    {
        wifi_rssi = ap_info.rssi;
    }

    // Przelicz RSSI na jakość w procentach
    int quality_percent = rssi_to_quality_percent(wifi_rssi);

    // Pobierz numer uruchomienia z NVS
    nvs_handle_t nvs_handle;
    int32_t boot_count = 0;

    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK)
    {
        ret = nvs_get_i32(nvs_handle, "boot_count", &boot_count);
        if (ret == ESP_ERR_NVS_NOT_FOUND)
        {
            boot_count = 0; // Pierwsze uruchomienie
        }
        boot_count++;
        nvs_set_i32(nvs_handle, "boot_count", boot_count);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    // Wysłanie informacji o jakości WiFi
    char quality_str[16];
    snprintf(quality_str, sizeof(quality_str), "%d", quality_percent);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_WIFI_QUALITY, quality_str, 0, 1, 1); // retained = 1

    // // Wysłanie numeru uruchomienia
    // char boot_str[16];
    // snprintf(boot_str, sizeof(boot_str), "%ld", boot_count);
    // esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_BOOT_COUNT, boot_str, 0, 1, 1); // retained = 1

    ESP_LOGI(TAG, "Connection info sent:");
    ESP_LOGI(TAG, "WiFi Quality: %d%% (RSSI: %d dBm)", quality_percent, wifi_rssi);
    ESP_LOGI(TAG, "Boot Count: %ld", boot_count);

    connection_info_sent = true;
}

// Handler zdarzeń WiFi z szczegółowym debugowaniem
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "📡 WiFi station started");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "❌ WiFi disconnected! Reason: %d (%s)", disconnected->reason,
                 disconnected->reason == 2 ? "AUTH_EXPIRE" : disconnected->reason == 4 ? "PROBE_REQ_TIMEOUT"
                                                         : disconnected->reason == 8   ? "ASSOC_LEAVE"
                                                         : disconnected->reason == 15  ? "4WAY_HANDSHAKE_TIMEOUT"
                                                         : disconnected->reason == 201 ? "NO_AP_FOUND"
                                                         : disconnected->reason == 205 ? "AUTH_FAIL"
                                                                                       : "OTHER");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "🔄 Attempting to reconnect...");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "✅ WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// Handler zdarzeń MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "🔗 MQTT connected to broker");
        mqtt_connected = true;
        xEventGroupSetBits(s_wifi_event_group, MQTT_CONNECTED_BIT);

        send_connection_info();

        // Subskrybuj się na topic do odbierania komend/konfiguracji
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CURRENT_SET_TOTAL, 1);
        if (msg_id >= 0)
        {
            ESP_LOGI(TAG, "📥 Subscribed to commands topic (msg_id=%d)", msg_id);
        }
        else
        {
            ESP_LOGW(TAG, "⚠️ Failed to subscribe to commands topic");
        }

        msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CURRENT_GET_TOTAL, 1);
        if (msg_id >= 0)
        {
            ESP_LOGI(TAG, "📥 Subscribed to commands topic (msg_id=%d)", msg_id);
        }
        else
        {
            ESP_LOGW(TAG, "⚠️ Failed to subscribe to commands topic");
        }

        msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CURRENT_GET_CURRENT, 1);
        if (msg_id >= 0)
        {
            ESP_LOGI(TAG, "📥 Subscribed to commands topic (msg_id=%d)", msg_id);
        }
        else
        {
            ESP_LOGW(TAG, "⚠️ Failed to subscribe to commands topic");
        }

        msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CURRENT_SET_PRICE_ONE_KWH, 1);
        if (msg_id >= 0)
        {
            ESP_LOGI(TAG, "📥 Subscribed to commands topic (msg_id=%d)", msg_id);
        }
        else
        {
            ESP_LOGW(TAG, "⚠️ Failed to subscribe to commands topic");
        }

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

        if (parse_mqtt_data_callback != NULL)
        {
            parse_mqtt_data_callback(event->topic, event->data);
        }
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
    if (mqtt_client == NULL)
    {
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
        .ssid = (uint8_t *)WIFI_SSID,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 120,
        .scan_time.active.max = 150,
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0)
    {
        ESP_LOGW(TAG, "❌ No access points found!");
        return;
    }

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));

    ESP_LOGI(TAG, "📡 Found %d access points (with external antenna):", ap_count);
    bool target_found = false;

    // Pokaż wszystkie znalezione sieci z oceną sygnału
    for (int i = 0; i < ap_count; i++)
    {
        const char *signal_quality =
            ap_list[i].rssi >= -50 ? "EXCELLENT" : ap_list[i].rssi >= -60 ? "VERY GOOD"
                                               : ap_list[i].rssi >= -70   ? "GOOD"
                                               : ap_list[i].rssi >= -80   ? "FAIR"
                                               : ap_list[i].rssi >= -90   ? "WEAK"
                                                                          : "VERY WEAK";

        const char *auth_type =
            ap_list[i].authmode == WIFI_AUTH_OPEN ? "OPEN" : ap_list[i].authmode == WIFI_AUTH_WEP        ? "WEP"
                                                         : ap_list[i].authmode == WIFI_AUTH_WPA_PSK      ? "WPA"
                                                         : ap_list[i].authmode == WIFI_AUTH_WPA2_PSK     ? "WPA2"
                                                         : ap_list[i].authmode == WIFI_AUTH_WPA_WPA2_PSK ? "WPA/WPA2"
                                                                                                         : "OTHER";

        ESP_LOGI(TAG, "   [%d] '%s' | %d dBm (%s) | Ch:%d | %s",
                 i, ap_list[i].ssid, ap_list[i].rssi, signal_quality,
                 ap_list[i].primary, auth_type);

        if (strcmp((char *)ap_list[i].ssid, WIFI_SSID) == 0)
        {
            target_found = true;
            ESP_LOGI(TAG, "🎯 TARGET FOUND: '%s' | RSSI: %d dBm (%s)",
                     WIFI_SSID, ap_list[i].rssi, signal_quality);

            if (ap_list[i].rssi < -80)
            {
                ESP_LOGW(TAG, "⚠️ STILL WEAK! External antenna helped but signal is still low");
                ESP_LOGW(TAG, "   Consider moving closer or using directional antenna");
            }
            else if (ap_list[i].rssi < -70)
            {
                ESP_LOGI(TAG, "✅ BETTER! External antenna improved signal (should work)");
            }
            else
            {
                ESP_LOGI(TAG, "🚀 EXCELLENT! External antenna provides strong signal");
            }
        }
    }

    if (!target_found)
    {
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
            .scan_method = WIFI_FAST_SCAN,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

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

    // Sprawdzenie dostępnych sieci i siły sygnału
    scan_and_check_signal();

    ESP_LOGI(TAG, "🔌 Connecting to network...");
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "⏳ Waiting for connection (timeout: %d ms)", WIFI_TIMEOUT_MS);
    int bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                   pdFALSE, pdTRUE, WIFI_TIMEOUT_MS / portTICK_PERIOD_MS);

    bool connected = (bits & WIFI_CONNECTED_BIT) != 0;

    if (connected)
    {
        ESP_LOGI(TAG, "🎉 WiFi connection successful!");

        // 🔋 Włącz WiFi Modem Sleep dla oszczędzania energii
        ESP_LOGI(TAG, "🔋 Enabling WiFi Modem Sleep (power save mode)");
        esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        if (ps_ret == ESP_OK)
        {
            ESP_LOGI(TAG, "✅ WiFi Modem Sleep enabled");
            ESP_LOGI(TAG, "   📉 Expected power: ~20-30 mA (instead of 70-100 mA)");
            ESP_LOGI(TAG, "   📡 WiFi stays connected, radio sleeps between beacons");
        }
        else
        {
            ESP_LOGW(TAG, "⚠️ Failed to enable Modem Sleep: %s", esp_err_to_name(ps_ret));
        }
    }
    else
    {
        ESP_LOGE(TAG, "❌ WiFi connection timeout after %d ms", WIFI_TIMEOUT_MS);
    }

    return connected;
}

// Łączenie z MQTT broker
static bool mqtt_connect(void)
{
    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "❌ MQTT client not initialized!");
        return false;
    }

    ESP_LOGI(TAG, "🔗 Connecting to MQTT broker: %s", MQTT_BROKER_URI);
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "❌ Failed to start MQTT client: %s", esp_err_to_name(ret));
        return false;
    }

    // Czekaj na połączenie z MQTT (timeout 10 sekund)
    ESP_LOGI(TAG, "⏳ Waiting for MQTT connection...");
    int bits = xEventGroupWaitBits(s_wifi_event_group, MQTT_CONNECTED_BIT,
                                   pdFALSE, pdTRUE, 10000 / portTICK_PERIOD_MS);

    bool connected = (bits & MQTT_CONNECTED_BIT) != 0;

    if (connected)
    {
        ESP_LOGI(TAG, "🎉 MQTT connection successful!");
    }
    else
    {
        ESP_LOGE(TAG, "❌ MQTT connection timeout");
    }

    return connected;
}

// === PUBLICZNE FUNKCJE === //

esp_err_t communication_init(parse_mqtt_data_callback_t incoming_mqtt_data_callback)
{

    parse_mqtt_data_callback = incoming_mqtt_data_callback;

    if (module_initialized)
    {
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
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS flash initialized");

    // Inicjalizacja WiFi
    ret = wifi_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "❌ WiFi initialization failed");
        return ret;
    }
    ESP_LOGI(TAG, "✅ WiFi initialized");

    // Inicjalizacja MQTT
    ret = mqtt_init();
    if (ret != ESP_OK)
    {
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
    if (!module_initialized)
    {
        ESP_LOGE(TAG, "❌ Communication module not initialized!");
        return false;
    }

    return wifi_connect();
}

bool communication_connect_mqtt(void)
{
    if (!module_initialized)
    {
        ESP_LOGE(TAG, "❌ Communication module not initialized!");
        return false;
    }

    return mqtt_connect();
}

bool communication_publish_data(const char *topic, const char *data)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "❌ MQTT not connected!");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, 0, 1, 1); // retained = 1
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "❌ Failed to publish MQTT message");
        return false;
    }

    ESP_LOGI(TAG, "📤 Published RETAINED to '%s': %s (msg_id=%d)", topic, data, msg_id);
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

    if (module_initialized)
    {
        if (mqtt_client)
        {
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

// === POWER MANAGEMENT === //

esp_err_t communication_enable_advanced_power_save(bool enable_light_sleep)
{
    ESP_LOGI(TAG, "🔋 Configuring Advanced Power Management");
    ESP_LOGI(TAG, "   Light Sleep: %s", enable_light_sleep ? "ENABLED" : "DISABLED");

    // Konfiguracja Power Management
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,                     // Maksymalna częstotliwość CPU: 160 MHz
        .min_freq_mhz = 40,                      // Minimalna częstotliwość CPU: 40 MHz (XTAL)
        .light_sleep_enable = enable_light_sleep // Automatyczny Light Sleep
    };

    esp_err_t ret = esp_pm_configure(&pm_config);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "✅ Power Management configured successfully");
        ESP_LOGI(TAG, "   📊 CPU Frequency: %d MHz → %d MHz (dynamic)",
                 pm_config.min_freq_mhz, pm_config.max_freq_mhz);

        if (enable_light_sleep)
        {
            ESP_LOGI(TAG, "   💤 Light Sleep: ENABLED");
            ESP_LOGI(TAG, "      • CPU sleeps when idle");
            ESP_LOGI(TAG, "      • Wakes on: WiFi beacon, UART, GPIO, Timer");
            ESP_LOGI(TAG, "      • Expected power: 5-10 mA in sleep");
            ESP_LOGI(TAG, "   ⚠️  NOTE: UART RX may need PM lock to prevent sleep!");
        }
        else
        {
            ESP_LOGI(TAG, "   ⚡ Dynamic Frequency Scaling: ENABLED");
            ESP_LOGI(TAG, "      • CPU slows down when idle (160→40 MHz)");
            ESP_LOGI(TAG, "      • Expected power: 15-25 mA average");
        }

        ESP_LOGI(TAG, "   📉 Total power with WiFi Modem Sleep:");
        ESP_LOGI(TAG, "      • WiFi active: ~20-30 mA");
        ESP_LOGI(TAG, "      • With DFS: ~15-20 mA");
        if (enable_light_sleep)
        {
            ESP_LOGI(TAG, "      • With Light Sleep: ~5-10 mA");
        }
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        ESP_LOGE(TAG, "❌ Power Management not supported!");
        ESP_LOGE(TAG, "   Enable CONFIG_PM_ENABLE in menuconfig:");
        ESP_LOGE(TAG, "   Component config → Power Management → [*] Enable PM");
        return ret;
    }
    else
    {
        ESP_LOGE(TAG, "❌ Failed to configure PM: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

// === NTP TIME SYNC === //

bool sync_time_from_ntp(void)
{

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);

    tzset(); // Aktywuj strefę czasową w systemie

    // Zatrzymaj SNTP jeśli już działa, żeby uniknąć asserta w sntp_setoperatingmode
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL); // Tryb odpytywania (zapytaj serwer o czas)

    esp_sntp_setservername(0, "pool.ntp.org"); // Ustaw serwer czasu - "pool.ntp.org" to publiczny serwer NTP

    esp_sntp_init(); // Uruchom klienta SNTP

    // KROK 3: Czekaj na synchronizację (maksymalnie 5 sekund)
    for (int i = 0; i < 5 &&
                    esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED;
         i++)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Czekaj 1 sekundę
    }

    // KROK 4: Zwróć true jeśli udało się pobrać czas
    return (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

esp_err_t establish_communication()
{

    // Sprawdź stan przycisku na pinie D7 (GPIO 6)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    vTaskDelay(pdMS_TO_TICKS(100));

    // GPIO configuration
    int button_state = gpio_get_level(BOOT_BUTTON_GPIO);

    ESP_LOGI(TAG, "🔘 Button state on GPIO6 (D7): %s", button_state ? "RELEASED" : "PRESSED");

    if (button_state == 0) // Przycisk naciśnięty (zwarty do masy)
    {
        ESP_LOGI(TAG, "🔧 Button pressed - entering WiFi AP mode for configuration");
        communication_create_wifi_ap();
        wifi_mode = WIFI_IN_AP_MODE;
        return ESP_OK;
    }
    else
    {

        // Inicjalizacja modułu komunikacji
        esp_err_t ret = communication_init(&parse_mqtt_message);
        if (ret == ESP_OK)
        {
            // Łączenie z WiFi
            bool wifi_connected = communication_connect_wifi();
            if (wifi_connected)
            {

                bool mqtt_connected = communication_connect_mqtt();
                if (mqtt_connected)
                {
                    wifi_mode = WIFI_IN_NORMAL_MODE;
                    // Krótka zwłoka na dokończenie transmisji
                    vTaskDelay(pdMS_TO_TICKS(800));
                    return ESP_OK;
                }
            }
            else
            {
                communication_create_wifi_ap();
                wifi_mode = WIFI_IN_NORMAL_MODE;
                return ESP_FAIL;
            }
            // communication_cleanup();
        }
        else
        {
            return ESP_FAIL;
        }
        return ESP_FAIL;
    }
}

esp_err_t communication_create_wifi_ap()
{
    ESP_LOGI(TAG, "🔧 Creating WiFi Access Point for configuration");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicjalizacja stosu sieciowego i event loop (jeśli jeszcze nie zainicjowane)
    esp_netif_init();
    esp_event_loop_create_default();

    // Stwórz domyślny netif dla AP - to uruchamia DHCP server!
    esp_netif_create_default_wifi_ap();

    // Inicjalizacja WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "Licznik_ustawienia",
            .ssid_len = strlen("Licznik_ustawienia"),
            .password = "konfiguracja",
            .channel = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "✅ Access Point 'Licznik_ustawienia' started");
    ESP_LOGI(TAG, "🔑 Password: konfiguracja");
    ESP_LOGI(TAG, "🌐 IP: 192.168.4.1");

    // Uruchom serwer HTTP od razu
    return create_configuration_html_page();
}

esp_err_t config_page_handler(httpd_req_t *req)
{
    // Odczyt aktualnych wartości z NVS/flash
    char saved_ssid[64] = "";
    char saved_password[64] = "";
    uint32_t kwh_total_raw = 0;
    uint32_t price_raw = 0;
    uint32_t report_time_raw = 300; // domyślnie 05:00

    // Odczyt WiFi z NVS
    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t len = sizeof(saved_ssid);
        nvs_get_str(nvs_handle, "ssid", saved_ssid, &len);
        len = sizeof(saved_password);
        nvs_get_str(nvs_handle, "password", saved_password, &len);
        nvs_close(nvs_handle);
    }

    // Odczyt z flash managera
    flash_manager_read_uint32_t(FLASH_TOTAL_CONSUMPTION, &kwh_total_raw);
    flash_manager_read_uint32_t(FLASH_PRICE_ONE_KWH, &price_raw);
    flash_manager_read_uint32_t(FLASH_REPORT_TIME, &report_time_raw);

    float kwh_total = kwh_total_raw / 100.0f;
    float price_zl  = price_raw / 1000.0f;
    int report_hour = report_time_raw / 60;
    int report_min  = report_time_raw % 60;

    // Bufor na dynamiczną stronę (~3KB)
    char *page = malloc(3200);
    if (!page) { httpd_resp_send_500(req); return ESP_FAIL; }

    snprintf(page, 3200,
        "<!DOCTYPE html>"
        "<html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Licznik energii - Konfiguracja</title>"
        "<style>"
        "body{font-family:Arial;margin:0;background:#f0f0f0;}"
        ".container{max-width:440px;margin:0 auto;padding:16px;}"
        "h1{color:#333;text-align:center;font-size:20px;margin-bottom:4px;}"
        ".subtitle{text-align:center;color:#666;font-size:13px;margin-bottom:16px;}"
        ".card{background:white;padding:18px;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,0.1);margin-bottom:14px;}"
        ".card h2{font-size:15px;color:#444;margin:0 0 12px 0;padding-bottom:8px;border-bottom:2px solid #f0f0f0;}"
        "label{display:block;font-size:13px;color:#555;margin-bottom:3px;}"
        "input{width:100%%;padding:9px 10px;margin-bottom:10px;border:1px solid #ddd;border-radius:6px;"
        "box-sizing:border-box;font-size:14px;}"
        "input:focus{outline:none;border-color:#007bff;box-shadow:0 0 0 2px rgba(0,123,255,0.15);}"
        ".btn{width:100%%;padding:11px;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold;}"
        ".btn-blue{background:#007bff;color:white;}"
        ".btn-green{background:#28a745;color:white;}"
        ".btn-orange{background:#fd7e14;color:white;}"
        ".btn:hover{opacity:0.88;}"
        ".info{background:#e7f3ff;padding:9px 11px;border-radius:6px;font-size:12px;color:#555;margin-bottom:10px;}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>&#9889; Licznik energii</h1>"
        "<div class='subtitle'>Panel konfiguracyjny &bull; 192.168.4.1</div>"

        // WiFi
        "<div class='card'>"
        "<h2>&#128225; Konfiguracja WiFi</h2>"
        "<div class='info'>Dane sieci WiFi, do której ma sie podlaczac licznik.</div>"
        "<form action='/save_wifi' method='POST'>"
        "<label>Nazwa sieci (SSID)</label>"
        "<input type='text' name='ssid' value='%s' required>"
        "<label>Haslo</label>"
        "<input type='password' name='password' value='%s'>"
        "<button type='submit' class='btn btn-blue'>&#128190; Zapisz WiFi i uruchom ponownie</button>"
        "</form></div>"

        // Licznik
        "<div class='card'>"
        "<h2>&#128290; Stan poczatkowy licznika</h2>"
        "<div class='info'>Aktualny odczyt z licznika (kWh).</div>"
        "<form action='/save_counter' method='POST'>"
        "<label>Stan licznika [kWh]</label>"
        "<input type='number' name='kwh' value='%.2f' step='0.01' min='0' required>"
        "<button type='submit' class='btn btn-green'>&#10003; Zapisz stan licznika</button>"
        "</form></div>"

        // Cena
        "<div class='card'>"
        "<h2>&#128176; Cena energii</h2>"
        "<div class='info'>Aktualna cena za 1 kWh energii elektrycznej.</div>"
        "<form action='/save_price' method='POST'>"
        "<label>Cena za 1 kWh [zl]</label>"
        "<input type='number' name='price' value='%.3f' step='0.001' min='0' required>"
        "<button type='submit' class='btn btn-green'>&#128190; Zapisz cene</button>"
        "</form></div>"

        // Raport
        "<div class='card'>"
        "<h2>&#128202; Godzina raportu dobowego</h2>"
        "<div class='info'>O ktorej godzinie wysylac dobowy raport zuzycia.</div>"
        "<form action='/save_report' method='POST'>"
        "<label>Godzina wysylki raportu</label>"
        "<input type='time' name='report_time' value='%02d:%02d' required>"
        "<button type='submit' class='btn btn-orange'>&#9200; Zapisz godzine raportu</button>"
        "</form></div>"

        "</div></body></html>",
        saved_ssid, saved_password,
        kwh_total,
        price_zl,
        report_hour, report_min
    );

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    free(page);
    return ESP_OK;
}

esp_err_t save_wifi_handler(httpd_req_t *req)
{
    char content[512];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    content[received] = '\0';

    char decoded_ssid[64] = {0};
    char decoded_password[64] = {0};
    httpd_query_key_value(content, "ssid", decoded_ssid, sizeof(decoded_ssid));
    httpd_query_key_value(content, "password", decoded_password, sizeof(decoded_password));

    ESP_LOGI(TAG, "📝 Zapisuję WiFi: SSID='%s'", decoded_ssid);

    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK)
    {
        nvs_set_str(nvs_handle, "ssid", decoded_ssid);
        nvs_set_str(nvs_handle, "password", decoded_password);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "✅ Konfiguracja WiFi zapisana");
    }

    const char *page =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='font-family:Arial;text-align:center;padding:30px'>"
        "<h2 style='color:#28a745'>✅ WiFi zapisane!</h2>"
        "<p>Urządzenie uruchomi się ponownie za 3 sekundy...</p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

esp_err_t save_counter_handler(httpd_req_t *req)
{
    char content[256];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    content[received] = '\0';

    char kwh_str[32] = {0};
    httpd_query_key_value(content, "kwh", kwh_str, sizeof(kwh_str));

    float kwh_value = atof(kwh_str);
    uint32_t kwh_wh = (uint32_t)(kwh_value * 100); // zapisujemy jako 0.01 kWh

    ESP_LOGI(TAG, "🔢 Ustawiam stan licznika: %.2f kWh (%lu x0.01kWh)", kwh_value, (unsigned long)kwh_wh);

    flash_manager_save_uint32_t(FLASH_TOTAL_CONSUMPTION, kwh_wh);

    char resp[512];
    snprintf(resp, sizeof(resp),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
        "<body style='font-family:Arial;text-align:center;padding:30px'>"
        "<h2 style='color:#28a745'>✅ Stan licznika zapisany!</h2>"
        "<p>Nowy stan: <strong>%.2f kWh</strong></p>"
        "<a href='/'>← Wróć do panelu</a>"
        "</body></html>", kwh_value);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t save_report_handler(httpd_req_t *req)
{
    char content[256];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    content[received] = '\0';

    char report_time[8] = {0};
    httpd_query_key_value(content, "report_time", report_time, sizeof(report_time));

    // Parsuj HH:MM
    int hour = 0, minute = 0;
    sscanf(report_time, "%d:%d", &hour, &minute);
    uint32_t time_encoded = (uint32_t)(hour * 60 + minute); // minuty od północy

    ESP_LOGI(TAG, "⏰ Ustawiam godzinę raportu: %02d:%02d (=%lu min)", hour, minute, (unsigned long)time_encoded);

    flash_manager_save_uint32_t(FLASH_REPORT_TIME, time_encoded);

    char resp[512];
    snprintf(resp, sizeof(resp),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
        "<body style='font-family:Arial;text-align:center;padding:30px'>"
        "<h2 style='color:#fd7e14'>✅ Godzina raportu zapisana!</h2>"
        "<p>Raport będzie wysyłany codziennie o <strong>%02d:%02d</strong></p>"
        "<a href='/'>← Wróć do panelu</a>"
        "</body></html>", hour, minute);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t save_price_handler(httpd_req_t *req)
{
    char content[256];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    content[received] = '\0';

    char price_str[32] = {0};
    httpd_query_key_value(content, "price", price_str, sizeof(price_str));

    float price_value = atof(price_str);
    uint32_t price_encoded = (uint32_t)(price_value * 1000); // zapisujemy jako 0.001 zł

    ESP_LOGI(TAG, "💰 Ustawiam cenę 1 kWh: %.3f zł (%lu x0.001zł)", price_value, (unsigned long)price_encoded);

    flash_manager_save_uint32_t(FLASH_PRICE_ONE_KWH, price_encoded);

    char resp[512];
    snprintf(resp, sizeof(resp),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
        "<body style='font-family:Arial;text-align:center;padding:30px'>"
        "<h2 style='color:#28a745'>✅ Cena zapisana!</h2>"
        "<p>Nowa cena: <strong>%.3f zł / kWh</strong></p>"
        "<a href='/'>← Wróć do panelu</a>"
        "</body></html>", price_value);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t create_configuration_html_page()
{
    ESP_LOGI(TAG, "🔧 Starting HTTP configuration server");

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "❌ Błąd uruchomienia HTTP servera");
        return ESP_FAIL;
    }

    httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET,
        .handler = config_page_handler, .user_ctx = NULL
    };
    httpd_uri_t uri_save_wifi = {
        .uri = "/save_wifi", .method = HTTP_POST,
        .handler = save_wifi_handler, .user_ctx = NULL
    };
    httpd_uri_t uri_save_counter = {
        .uri = "/save_counter", .method = HTTP_POST,
        .handler = save_counter_handler, .user_ctx = NULL
    };
    httpd_uri_t uri_save_report = {
        .uri = "/save_report", .method = HTTP_POST,
        .handler = save_report_handler, .user_ctx = NULL
    };
    httpd_uri_t uri_save_price = {
        .uri = "/save_price", .method = HTTP_POST,
        .handler = save_price_handler, .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_save_wifi);
    httpd_register_uri_handler(server, &uri_save_counter);
    httpd_register_uri_handler(server, &uri_save_report);
    httpd_register_uri_handler(server, &uri_save_price);

    ESP_LOGI(TAG, "🌐 HTTP server started - otwórz http://192.168.4.1 w przeglądarce");

    return ESP_OK;
}