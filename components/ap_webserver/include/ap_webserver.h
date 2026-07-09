#ifndef AP_WEBSERVER_H
#define AP_WEBSERVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tryb pracy wyjścia GPIO udostępnionego przez panel WWW.
 */
typedef enum {
    AP_WEBSERVER_GPIO_TOGGLE = 0, /*!< Zwykłe wyjście on/off — kliknięcie trwale przełącza stan (WLACZONE/WYLACZONE) */
    AP_WEBSERVER_GPIO_GATE,       /*!< Wyjście impulsowe (np. sterownik bramy) — kliknięcie daje krótki impuls
                                       (~0.5s) na pinie, a stan logiczny "otwarta/zamknięta" jest śledzony
                                       programowo i przełączany przy każdym kliknięciu. */
} ap_webserver_gpio_mode_t;

/**
 * @brief Pojedyncze wyjście GPIO udostępnione do sterowania przez panel WWW.
 */
typedef struct {
    gpio_num_t pin;                /*!< Numer GPIO sterowany jako wyjście */
    const char *name;               /*!< Etykieta wyświetlana na stronie (np. "Brama") */
    ap_webserver_gpio_mode_t mode;  /*!< Tryb pracy: zwykłe on/off lub impulsowa brama */
    bool active_low;                /*!< true dla przekaźników aktywnych stanem niskim (typowe tanie
                                          moduły przekaźnikowe: LOW = załączony, HIGH = wyłączony).
                                          Gdy true, stan spoczynkowy/"wyłączony" to pin HIGH, a
                                          załączenie/impuls to pin LOW — dzięki temu domyślny stan
                                          po starcie (i podczas boot-u) jest bezpieczny (przekaźnik
                                          wyłączony), zamiast przypadkowo się załączać. */
} ap_webserver_gpio_t;

/**
 * @brief Konfiguracja modułu Access Point + serwer HTTP z logowaniem.
 */
typedef struct {
    const char *ap_ssid;            /*!< Nazwa sieci WiFi (SSID) tworzonej przez ESP32 */
    const char *ap_password;        /*!< Hasło WiFi (min. 8 znaków) lub NULL/"" dla sieci otwartej */
    uint8_t ap_channel;             /*!< Kanał WiFi (1-13), 0 = domyślny (1) */
    uint8_t ap_max_conn;             /*!< Maksymalna liczba klientów WiFi, 0 = domyślnie (4) */

    /* --- Oszczędzanie energii ---
     * Uwaga: w trybie SoftAP (ESP jako punkt dostępowy) nie da się włączyć modem-sleep
     * tak jak w trybie stacji (STA) — AP musi stale nadawać ramki beacon i nasłuchiwać,
     * więc esp_wifi_set_ps() tu nic nie zmienia. Poniższe pola to realne dźwignie
     * dostępne dla AP: niższa moc nadawania i rzadsze beacony = mniejszy pobór prądu radia.
     */
    int8_t ap_tx_power_dbm;          /*!< Maks. moc nadawania WiFi w dBm (typowo 2-20), 0 = bez zmian (domyślna, ~20dBm) */
    uint16_t ap_beacon_interval_ms;  /*!< Odstęp między ramkami beacon w ms, 0 = domyślnie 100. Wyższa wartość (np. 300-1000) obniża pobor prądu kosztem wolniejszego (re)łączenia klientów */

    const char *login_password;     /*!< 8-znakowe hasło logowania do panelu WWW, np. "Bobik111" */

    const ap_webserver_gpio_t *gpios; /*!< Tablica wyjść GPIO udostępnionych do sterowania */
    size_t gpio_count;                /*!< Liczba elementów w tablicy gpios */

    /* --- Krańcówka (wejście, opcjonalne) ---
     * Podłącz krańcówkę pomiędzy GND a poniższym pinem. Pin jest skonfigurowany
     * z wewnętrznym pull-up, więc styk zwarty do GND = stan "aktywny" (0 na pinie).
     */
    gpio_num_t limit_switch_pin;     /*!< Pin wejściowy krańcówki, -1 (GPIO_NUM_NC) = wyłączone */
    const char *limit_switch_name;   /*!< Etykieta na stronie, np. "Krancowka bramy" */
} ap_webserver_config_t;

/**
 * @brief Uruchamia WiFi w trybie Access Point oraz serwer HTTP z panelem logowania.
 *
 * Inicjalizuje NVS (jeśli jeszcze nie zainicjalizowane), stos sieciowy, WiFi w trybie AP
 * oraz serwer HTTP obsługujący:
 *  - GET  "/"        - strona logowania (formularz z hasłem)
 *  - POST "/login"   - weryfikacja hasła, ustawienie ciasteczka sesji
 *  - GET  "/panel"   - panel sterowania wyjściami (wymaga zalogowania)
 *  - POST "/toggle"  - przełączenie stanu wybranego GPIO (wymaga zalogowania)
 *  - GET  "/logout"  - wylogowanie
 *
 * Po połączeniu telefonem z siecią `ap_ssid` należy w przeglądarce wejść na adres
 * http://192.168.4.1/ i podać skonfigurowane hasło logowania.
 *
 * @param config Konfiguracja (SSID/hasło AP, hasło logowania, lista GPIO).
 * @return ESP_OK w przypadku sukcesu, kod błędu w przeciwnym razie.
 */
esp_err_t ap_webserver_start(const ap_webserver_config_t *config);

/**
 * @brief Zatrzymuje serwer HTTP oraz WiFi AP uruchomione przez ap_webserver_start().
 */
esp_err_t ap_webserver_stop(void);

/**
 * @brief Wyzwala impuls bramy dla pierwszego GPIO w trybie AP_WEBSERVER_GPIO_GATE.
 *
 * Używane m.in. przez ESP-NOW, aby wykorzystać dokładnie tę samą logikę sterowania
 * co panel WWW (bez duplikowania kodu).
 */
esp_err_t ap_webserver_trigger_gate(void);

/**
 * @brief Pobiera aktualny logiczny stan bramy (OTWARTA/ZAMKNIETA).
 * @return true jeśli istnieje GPIO w trybie bramy i stan został zwrócony.
 */
bool ap_webserver_get_gate_state(bool *is_open);

/**
 * @brief Pobiera aktualny stan krańcówki (aktywny = zwarta do GND).
 * @return true jeśli krańcówka jest skonfigurowana i stan został zwrócony.
 */
bool ap_webserver_get_limit_switch_state(bool *is_active);

/**
 * @brief Aktualizuje informacje ESP-NOW wyświetlane w panelu WWW.
 *
 * @param channel Kanał pracy ESP-NOW/AP.
 * @param self_mac Adres MAC tego urządzenia (6 bajtów).
 * @param peer_mac Adres MAC peera (6 bajtów) lub NULL, gdy brak.
 */
void ap_webserver_set_espnow_info(uint8_t channel, const uint8_t self_mac[6], const uint8_t *peer_mac);

#ifdef __cplusplus
}
#endif

#endif // AP_WEBSERVER_H
