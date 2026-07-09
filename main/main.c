#include <stdio.h>
#include <time.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "config.h"     
#include "nvs.h"
#include "ap_webserver.h"
#include "espnow_bridge.h"

// Tag dla logów
static const char *TAG = "MAIN";

#define ESPNOW_PEER_MAC_STR ""  // np. "24:6F:28:AA:BB:CC"; pusty = tylko odbior komend

static bool parse_mac_str(const char *mac_str, uint8_t out[6])
{
    if (!mac_str || strlen(mac_str) < 17) {
        return false;
    }
    unsigned int b[6];
    if (sscanf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)b[i];
    }
    return true;
}

// Wyjścia sterowane z panelu WWW (dopasuj piny do swojej płytki)
// active_low = true: typowe tanie moduły przekaźnikowe są aktywne stanem niskim
// (LOW = przekaźnik załączony, HIGH = wyłączony). Dzięki temu domyślny/startowy
// stan pinu (wyłączony = HIGH) nie załącza przekaźnika przy uruchomieniu ESP.
// Jeśli Twój moduł przekaźnikowy jest aktywny stanem wysokim, ustaw false.
static const ap_webserver_gpio_t s_outputs[] = {
    { .pin = GPIO_NUM_20, .name = "Brama", .mode = AP_WEBSERVER_GPIO_GATE, .active_low = true },
};

int main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ap_webserver_config_t ap_cfg = {
        .ap_ssid = "access",
        .ap_password = "22446688",              // "" = sie otwarta, lub podaj haslo WiFi (min. 8 znakow)
        .ap_channel = 1,
        .ap_max_conn = 4,
        .ap_tx_power_dbm = 12,           // nizsza moc nadawania = mniejszy pobor pradu (0 = domyslna ~20dBm)
        .ap_beacon_interval_ms = 1000,    // rzadsze beacony = mniejszy pobor pradu (0 = domyslne 100ms)
        .login_password = "Bobik111",   // 8-znakowe haslo logowania do panelu WWW
        .gpios = s_outputs,
        .gpio_count = sizeof(s_outputs) / sizeof(s_outputs[0]),
        .limit_switch_pin = GPIO_NUM_10,      // krancowka: pin -> GND, drugi koniec pinu ma pull-up
        .limit_switch_name = "Krancowka bramy",
    };

    ESP_ERROR_CHECK(ap_webserver_start(&ap_cfg));

    espnow_bridge_config_t espnow_cfg = {
        .channel = ap_cfg.ap_channel,
        .has_peer = false,
        .peer_mac = {0},
        .status_interval_ms = 5000,
    };
    espnow_cfg.has_peer = parse_mac_str(ESPNOW_PEER_MAC_STR, espnow_cfg.peer_mac);
    ESP_ERROR_CHECK(espnow_bridge_start(&espnow_cfg));

    ESP_LOGI(TAG, "Polacz sie z siecia WiFi \"%s\" i wejdz na http://192.168.4.1/", ap_cfg.ap_ssid);

    return 0;
}

