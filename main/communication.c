#include "communication.h"

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
#include "esp_sntp.h"
#include "esp_http_server.h"
#include "gaz_counter.h"
#include "mqtt_prefix.h"

#if defined(PAULINA)
// 💝 Konfiguracja Pauliny
#define WIFI_SSID "Dom"
#define WIFI_PASS "paula1234"
// #define WIFI_SSID "FunBox2-9877"
// #define WIFI_PASS "22446688"
#define MQTT_BROKER_URI "mqtts://a51fd01c7c0b4e2b881c011bfbc0d781.s2.eu.hivemq.cloud:8883"
#define MQTT_USERNAME "paulina"
#define MQTT_PASSWORD "Metypret69"
#define MQTT_CLIENT_ID "esp32c3_sensor_paulina"
#elif defined(BOBIK)
// 🏠 Konfiguracja domyślna (Twoja)
#define WIFI_SSID "FunBox2-C259"
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
static int wifi_rssi = -100;              // Siła sygnału WiFi
static bool connection_info_sent = false; // Flaga czy wysłano już info o połączeniu

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

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

        // Publikuj status połączenia
        // esp_mqtt_client_publish(event->client, MQTT_TOPIC_STATUS, "ESP32-C3 connected", 0, 1, 1); // retained
        // ESP_LOGI(TAG, "📢 Published status: connected");

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
    // === Wczytaj SSID/hasło z NVS (zapisane przez panel konfiguracyjny) ===
    char ssid[65] = {0};
    char password[65] = {0};
    bool nvs_ok = false;

    nvs_handle_t nvs_h;
    if (nvs_open("wifi_config", NVS_READONLY, &nvs_h) == ESP_OK)
    {
        size_t len = 64;
        esp_err_t r1 = nvs_get_str(nvs_h, "ssid", ssid, &len);
        len = 64;
        esp_err_t r2 = nvs_get_str(nvs_h, "password", password, &len);
        nvs_close(nvs_h);

        if (r1 == ESP_OK && strlen(ssid) > 0)
        {
            nvs_ok = true;
            ESP_LOGI(TAG, "📖 WiFi z NVS: SSID='%s' (len:%zu)", ssid, strlen(ssid));
        }
    }

    // Fallback na makra — tylko jeśli NVS jest pusty
    if (!nvs_ok)
    {
        ESP_LOGW(TAG, "⚠️  Brak WiFi w NVS — używam domyślnych z kodu: '%s'", WIFI_SSID);
        strncpy(ssid, WIFI_SSID, 64);
        strncpy(password, WIFI_PASS, 64);
    }

    ESP_LOGI(TAG, "🌐 Łączę z WiFi: '%s'", ssid);

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .scan_method = WIFI_FAST_SCAN,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    // Null-terminate na wszelki wypadek
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_LOGI(TAG, "🔐 SSID='%s' Pass='%s'",
             wifi_config.sta.ssid, wifi_config.sta.password);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(78));

    ESP_ERROR_CHECK(esp_wifi_connect());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "⏳ Czekam na połączenie (timeout: %d ms)...", WIFI_TIMEOUT_MS);
    int bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                   pdFALSE, pdTRUE, WIFI_TIMEOUT_MS / portTICK_PERIOD_MS);
    bool connected = (bits & WIFI_CONNECTED_BIT) != 0;

    if (connected)
    {
        ESP_LOGI(TAG, "🎉 WiFi połączone!");
    }
    else
    {
        ESP_LOGE(TAG, "❌ Timeout połączenia WiFi po %d ms", WIFI_TIMEOUT_MS);
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

esp_err_t communication_init(void)
{
    if (module_initialized)
    {
        ESP_LOGW(TAG, " Communication module already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, " Initializing communication module");

    // Wyświetl aktywną konfigurację
#ifdef PAULINA
    ESP_LOGI(TAG, "Konfiguracja: PAULINA");
    ESP_LOGI(TAG, "WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, " MQTT: paulina@hivemq.cloud");
#else
    ESP_LOGI(TAG, " Konfiguracja: DOMYŚLNA");
    ESP_LOGI(TAG, " WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, " MQTT: polnocna27@hivemq.cloud");
#endif

    // Inicjalizacja NVS (wymagane dla WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, " NVS flash initialized");

    // Inicjalizacja WiFi
    ret = wifi_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, " WiFi initialization failed");
        return ret;
    }
    ESP_LOGI(TAG, " WiFi initialized");

    // Inicjalizacja MQTT
    ret = mqtt_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, " MQTT initialization failed");
        return ret;
    }
    ESP_LOGI(TAG, " MQTT initialized");

    module_initialized = true;
    ESP_LOGI(TAG, " Communication module ready");
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

bool sync_time_from_ntp(void)
{

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com"); // backup
    esp_sntp_init();

    ESP_LOGI(TAG, "⏳ Czekam na NTP sync (max 10s)...");
    for (int i = 0; i < 10 &&
                    esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED;
         i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "   NTP... %d/10", i + 1);
    }

    bool ok = (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
    if (ok)
    {
        time_t now = time(NULL);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        ESP_LOGI(TAG, "✅ NTP OK: %s", buf);
    }
    else
    {
        ESP_LOGW(TAG, "⚠️  NTP timeout — czas nie zsynchronizowany");
    }
    return ok;
}

// === PANEL KONFIGURACYJNY WiFi/MQTT ===

// Forward declarations
esp_err_t config_page_handler(httpd_req_t *req);
esp_err_t save_wifi_handler(httpd_req_t *req);
esp_err_t save_counter_handler(httpd_req_t *req);

esp_err_t communication_create_wifi_ap()
{
    ESP_LOGI(TAG, "🔧 Creating WiFi Access Point for gas meter configuration");

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
            .ssid = "Licznik_Gazu",
            .ssid_len = strlen("Licznik_Gazu"),
            .password = "11111111",
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

    ESP_LOGI(TAG, "✅ Access Point 'Licznik_Gazu' started");
    ESP_LOGI(TAG, "🔑 Password: konfiguracja");
    ESP_LOGI(TAG, "🌐 IP: 192.168.4.1");

    // Uruchom serwer HTTP
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    config.stack_size = 10240;  // zwiększony stos — handlery używają ~2KB lokalnych buforów

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "❌ Błąd uruchomienia HTTP servera");
        return ESP_FAIL;
    }

    httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = config_page_handler, .user_ctx = NULL};
    httpd_uri_t uri_save_wifi = {
        .uri = "/save_wifi", .method = HTTP_POST, .handler = save_wifi_handler, .user_ctx = NULL};
    httpd_uri_t uri_save_counter = {
        .uri = "/save_counter", .method = HTTP_POST, .handler = save_counter_handler, .user_ctx = NULL};

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_save_wifi);
    httpd_register_uri_handler(server, &uri_save_counter);

    ESP_LOGI(TAG, "🌐 HTTP server started - otwórz http://192.168.4.1 w przeglądarce");

    return ESP_OK;
}

esp_err_t config_page_handler(httpd_req_t *req)
{
    char saved_ssid[65] = {0};
    char saved_password[65] = {0};
    char saved_email[65] = {0};
    float current_gas = get_total_gas();

    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READONLY, &nvs_handle) == ESP_OK)
    {
        size_t len = 64;
        if (nvs_get_str(nvs_handle, "ssid", saved_ssid, &len) == ESP_OK)
        {
            saved_ssid[len] = '\0';
        }
        len = 64;
        if (nvs_get_str(nvs_handle, "password", saved_password, &len) == ESP_OK)
        {
            saved_password[len] = '\0';
        }
        len = 64;
        if (nvs_get_str(nvs_handle, "mqtt_email", saved_email, &len) == ESP_OK)
        {
            saved_email[len] = '\0';
        }
        nvs_close(nvs_handle);
    }

    char *page = malloc(3200);
    if (!page)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(page, 3200,
             "<!DOCTYPE html>"
             "<html><head><meta charset='UTF-8'>"
             "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
             "<title>Konfiguracja Licznika Gazu</title>"
             "<style>"
             "body{font-family:Arial;margin:0;background:#f0f0f0;}"
             ".container{max-width:440px;margin:0 auto;padding:16px;}"
             "h1{color:#1a73e8;text-align:center;font-size:20px;margin-bottom:4px;}"
             ".subtitle{text-align:center;color:#666;font-size:13px;margin-bottom:16px;}"
             ".card{background:white;padding:18px;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,0.1);margin-bottom:14px;}"
             ".card h2{font-size:15px;color:#444;margin:0 0 12px 0;padding-bottom:8px;border-bottom:2px solid #f0f0f0;}"
             "label{display:block;font-size:13px;color:#555;margin-bottom:3px;}"
             "input{width:100%%;padding:9px 10px;margin-bottom:10px;border:1px solid #ddd;border-radius:6px;"
             "box-sizing:border-box;font-size:14px;}"
             "input:focus{outline:none;border-color:#1a73e8;box-shadow:0 0 0 2px rgba(26,115,232,0.15);}"
             ".btn{width:100%%;padding:11px;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold;}"
             ".btn-blue{background:#1a73e8;color:white;}"
             ".btn-orange{background:#fd7e14;color:white;}"
             ".btn:hover{opacity:0.88;}"
             ".info{background:#e8f0fe;padding:9px 11px;border-radius:6px;font-size:12px;color:#555;margin-bottom:10px;}"
             ".current{background:#e7f5e7;padding:9px 11px;border-radius:6px;font-size:12px;color:#333;margin-bottom:10px;}"
             "</style></head><body>"
             "<div class='container'>"
             "<h1>&#128293; Licznik Gazu</h1>"
             "<div class='subtitle'>Panel konfiguracyjny &bull; 192.168.4.1</div>"

             "<div class='card'>"
             "<h2>&#128225; Konfiguracja WiFi i MQTT</h2>"
             "<div class='info'>Podaj dane sieci WiFi oraz adres e-mail &mdash; b&#281;dzie g&#322;&oacute;wnym tematem MQTT.</div>"
             "<form action='/save_wifi' method='POST'>"
             "<label>Nazwa sieci WiFi (SSID)</label>"
             "<input type='text' name='ssid' value='%s' required>"
             "<label>Has&#322;o WiFi</label>"
             "<input type='password' name='password' value='%s'>"
             "<label>Adres e-mail (prefiks temat&oacute;w MQTT)</label>"
             "<input type='email' name='email' value='%s' placeholder='jan.kowalski@gmail.com' required>"
             "<p style='font-size:11px;color:#888;margin-top:-6px;'>Tematy MQTT: <strong>&lt;email&gt;/gas/total_m3</strong>, <strong>/daily_m3</strong> itd.</p>"
             "<button type='submit' class='btn btn-blue'>&#128190; Zapisz i uruchom ponownie</button>"
             "</form></div>"

             "<div class='card'>"
             "<h2>&#128202; Stan licznika</h2>"
             "<div class='current'>Aktualny stan: <strong>%.3f m&sup3;</strong></div>"
             "<div class='info'>Je&#347;li nowy licznik lub po wymianie &mdash; ustaw aktualny stan z fizycznego licznika.</div>"
             "<form action='/save_counter' method='POST'>"
             "<label>Nowy stan licznika (m&sup3;)</label>"
             "<input type='number' step='0.001' name='gas_m3' value='%.3f' required>"
             "<button type='submit' class='btn btn-orange'>&#9881; Ustaw stan licznika</button>"
             "</form></div>"

             "</div></body></html>",
             saved_ssid, saved_password, saved_email,
             current_gas, current_gas);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    free(page);
    return ESP_OK;
}

// Dekoduje URL encoding: %40 → @, %20 → spacja, + → spacja itd.
static void url_decode(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    while (*src && i < dst_len - 1)
    {
        if (*src == '%' && src[1] && src[2])
        {
            char hex[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            dst[i++] = ' ';
            src++;
        }
        else
        {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

esp_err_t save_wifi_handler(httpd_req_t *req)
{
    // Bufor musi zmieścić pełne body: ssid(64) + pass(64) + email(64) + URL-encoding + klucze
    static char content[768];
    memset(content, 0, sizeof(content));

    // Czytaj CAŁE body
    int total = 0;
    int remaining = req->content_len;
    if (remaining <= 0 || remaining >= (int)sizeof(content)) {
        ESP_LOGE(TAG, "❌ Nieprawidłowy content_len: %d", remaining);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    while (remaining > 0) {
        int received = httpd_req_recv(req, content + total, remaining);
        if (received <= 0) {
            ESP_LOGE(TAG, "❌ Błąd odczytu body: %d", received);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        total     += received;
        remaining -= received;
    }
    content[total] = '\0';
    ESP_LOGI(TAG, "📥 POST body (%d B): %s", total, content);

    // Bufor surowych (URL-encoded) wartości
    char raw_ssid[65]     = {0};
    char raw_password[65] = {0};
    char raw_email[65]    = {0};
    httpd_query_key_value(content, "ssid",     raw_ssid,     sizeof(raw_ssid));
    httpd_query_key_value(content, "password", raw_password, sizeof(raw_password));
    httpd_query_key_value(content, "email",    raw_email,    sizeof(raw_email));

    // URL-dekodowanie
    char decoded_ssid[65]     = {0};
    char decoded_password[65] = {0};
    char decoded_email[65]    = {0};
    url_decode(decoded_ssid,     sizeof(decoded_ssid),     raw_ssid);
    url_decode(decoded_password, sizeof(decoded_password), raw_password);
    url_decode(decoded_email,    sizeof(decoded_email),    raw_email);

    ESP_LOGI(TAG, "📝 SSID='%s' (len:%zu) | pass len:%zu | email='%s' (len:%zu)",
             decoded_ssid, strlen(decoded_ssid),
             strlen(decoded_password),
             decoded_email, strlen(decoded_email));

    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK)
    {
        // SSID — zawsze zapisz jeśli niepuste
        if (strlen(decoded_ssid) > 0) {
            nvs_set_str(nvs_handle, "ssid", decoded_ssid);
        } else {
            ESP_LOGW(TAG, "⚠️  Puste SSID — pomijam zapis");
        }

        // Hasło — zapisz tylko jeśli użytkownik coś wpisał
        // (puste pole = zostaw stare hasło)
        if (strlen(decoded_password) > 0) {
            nvs_set_str(nvs_handle, "password", decoded_password);
        } else {
            ESP_LOGW(TAG, "⚠️  Puste hasło — zachowuję stare z NVS");
        }

        // Email — zawsze zapisz jeśli niepuste
        if (strlen(decoded_email) > 0) {
            nvs_set_str(nvs_handle, "mqtt_email", decoded_email);
        }

        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "✅ Konfiguracja zapisana do NVS");
    }

    static char resp[768];
    snprintf(resp, sizeof(resp),
             "<body style='font-family:Arial;text-align:center;padding:30px'>"
             "<h2 style='color:#1a73e8'>&#9989; Zapisano!</h2>"
             "<p>Urz&#261;dzenie uruchomi si&#281; ponownie za 3 sekundy...</p>"
             "<p style='color:#555;font-size:13px'>Tematy MQTT:<br>"
             "<strong>%s/gas/total_m3</strong><br>"
             "<strong>%s/gas/daily_m3</strong><br>"
             "<strong>%s/gas/pulse_count</strong> itd.</p>"
             "</body></html>",
             decoded_email, decoded_email, decoded_email);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

esp_err_t save_counter_handler(httpd_req_t *req)
{
    char content[256];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[received] = '\0';

    char gas_str[32] = {0};
    httpd_query_key_value(content, "gas_m3", gas_str, sizeof(gas_str));

    float gas_value = atof(gas_str);

    ESP_LOGI(TAG, "⛽ Ustawiam stan licznika gazu: %.3f m³", gas_value);

    // TODO: Dodaj funkcję do gaz_counter.c żeby ustawić stan licznika
    set_total_gas(gas_value);

    char resp[512];
    snprintf(resp, sizeof(resp),
             "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
             "<body style='font-family:Arial;text-align:center;padding:30px'>"
             "<h2 style='color:#28a745'>✅ Stan licznika zapisany!</h2>"
             "<p>Nowy stan: <strong>%.3f m&sup3;</strong></p>"
             "<a href='/'>← Wróć do panelu</a>"
             "</body></html>",
             gas_value);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void communication_check_and_handle_config_button(void)
{
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&button_config);

    vTaskDelay(pdMS_TO_TICKS(100));

    int button_state = gpio_get_level(BOOT_BUTTON_GPIO);
    ESP_LOGI(TAG, "🔘 Przycisk konfiguracji (GPIO%d): %s", BOOT_BUTTON_GPIO,
             button_state ? "ZWOLNIONY" : "NACIŚNIĘTY");

    if (button_state == 0)
    {
        // Przycisk naciśnięty → tryb konfiguracji AP
        ESP_LOGI(TAG, "🔧 Tryb konfiguracji - uruchamiam Access Point");
        communication_create_wifi_ap();
        // W trybie AP nie usypiamy — czekamy na konfigurację
        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void publish_timestamp()
{


    // Timestamp (tylko jeśli czas zsynchronizowany)
    time_t now = time(NULL);
    if (now > 1000000000)
    { // > rok 2001 → czas ustawiony
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
        communication_publish_data(mqtt_topic("timestamp"), ts);
        ESP_LOGI(TAG, "MQTT timestamp: %s", ts);
    }
}
void publish_wifi_quality()
{
    char data[32];

    // Jakość WiFi w procentach
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        int rssi = ap_info.rssi;
        int quality = rssi >= -50 ? 100 : rssi >= -60 ? 80 + (rssi + 60) * 2
                                      : rssi >= -70   ? 60 + (rssi + 70) * 2
                                      : rssi >= -80   ? 40 + (rssi + 80) * 2
                                      : rssi >= -90   ? 20 + (rssi + 90) * 2
                                                      : 0;
        snprintf(data, sizeof(data), "%d", quality);
        communication_publish_data(mqtt_topic("wifi_quality"), data);
        ESP_LOGI(TAG, "MQTT wifi_quality: %s%% (RSSI: %d dBm)",
                 data, rssi);
    }
}
       