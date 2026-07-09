#include <string.h>
#include <stdlib.h>
#include "ap_webserver.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ap_webserver";

#define AP_WEBSERVER_MAX_GPIOS      16
#define AP_WEBSERVER_SESSION_LEN    32  /* liczba znaków hex tokena sesji */
#define AP_WEBSERVER_GATE_PULSE_MS  500 /* czas trwania impulsu sterujacego brama */
#define AP_WEBSERVER_HTTPD_STACK    8192 /* domyslne 4096 jest za male dla naszych handlerow (bufor strony 2KB) */
#define AP_WEBSERVER_NVS_NAMESPACE  "ap_websrv"
#define AP_WEBSERVER_NVS_KEY_PWD    "login_pwd"

/* Skopiowana konfiguracja (nie polegamy na wskaźnikach przekazanych przez wywołującego) */
static char s_ssid[33];
static char s_password[65];
static char s_login_password[65];
static ap_webserver_gpio_t s_gpios[AP_WEBSERVER_MAX_GPIOS];
static bool s_gate_open[AP_WEBSERVER_MAX_GPIOS]; /* logiczny stan bramy (tylko dla trybu GATE) */
static size_t s_gpio_count;
static uint8_t s_channel;
static uint8_t s_max_conn;
static int8_t s_tx_power_dbm;
static uint16_t s_beacon_interval_ms;
static gpio_num_t s_limit_switch_pin = GPIO_NUM_NC;
static char s_limit_switch_name[32];

static httpd_handle_t s_server = NULL;
static bool s_session_active = false;
static char s_session_token[AP_WEBSERVER_SESSION_LEN + 1];
static uint8_t s_espnow_self_mac[6] = {0};
static uint8_t s_espnow_peer_mac[6] = {0};
static bool s_espnow_info_valid = false;
static bool s_espnow_peer_valid = false;

/* ---------- Pomocnicze ---------- */

/* Porównanie w stałym czasie — utrudnia atak typu timing attack na hasło. */
static bool safe_streq(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    size_t la = strlen(a);
    size_t lb = strlen(b);
    if (la != lb) {
        return false;
    }
    volatile unsigned char diff = 0;
    for (size_t i = 0; i < la; i++) {
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return diff == 0;
}

static void generate_session_token(void)
{
    uint8_t raw[AP_WEBSERVER_SESSION_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw); i++) {
        s_session_token[2 * i]     = hex[raw[i] >> 4];
        s_session_token[2 * i + 1] = hex[raw[i] & 0x0F];
    }
    s_session_token[AP_WEBSERVER_SESSION_LEN] = '\0';
}

/* Sprawdza nagłówek "Cookie: session=<token>" i porównuje z aktywną sesją. */
static bool request_is_authenticated(httpd_req_t *req)
{
    if (!s_session_active) {
        return false;
    }

    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (cookie_len == 0) {
        return false;
    }

    char *cookie = malloc(cookie_len + 1);
    if (!cookie) {
        return false;
    }

    bool authenticated = false;
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, cookie_len + 1) == ESP_OK) {
        const char *needle = "session=";
        char *pos = strstr(cookie, needle);
        if (pos) {
            pos += strlen(needle);
            char token[AP_WEBSERVER_SESSION_LEN + 1] = {0};
            size_t i = 0;
            while (pos[i] && pos[i] != ';' && i < AP_WEBSERVER_SESSION_LEN) {
                token[i] = pos[i];
                i++;
            }
            token[i] = '\0';
            authenticated = safe_streq(token, s_session_token);
        }
    }

    free(cookie);
    return authenticated;
}

/* Wczytuje ciało żądania (form-urlencoded) do bufora o zadanym rozmiarze. */
static esp_err_t read_request_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        return ESP_FAIL;
    }
    buf[received] = '\0';
    return ESP_OK;
}

static void format_mac(const uint8_t mac[6], char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Zwraca fizyczny poziom pinu odpowiadajacy stanowi "wylaczony"/"nieaktywny" (uwzglednia active_low). */
static inline int gpio_off_level(const ap_webserver_gpio_t *g)
{
    return g->active_low ? 1 : 0;
}

/* Zwraca fizyczny poziom pinu odpowiadajacy stanowi "zalaczony"/"aktywny" (uwzglednia active_low). */
static inline int gpio_on_level(const ap_webserver_gpio_t *g)
{
    return g->active_low ? 0 : 1;
}

/* Zwraca logiczny stan (true = zalaczony) na podstawie aktualnego fizycznego poziomu pinu. */
static inline bool gpio_is_on(const ap_webserver_gpio_t *g)
{
    return gpio_get_level(g->pin) == gpio_on_level(g);
}

/* Kontekst przekazywany do callbacku timera impulsu bramy (alokowany na stercie, zwalniany w callbacku). */
typedef struct {
    gpio_num_t pin;
    bool active_low;
    esp_timer_handle_t timer;
} gate_pulse_ctx_t;

/* Callback timera opuszczajacy pin po zakonczeniu impulsu bramy (wywolywany z zadania esp_timer,
 * nie blokuje watku httpd - zapobiega przepelnieniu stosu i blokowaniu innych zadan). */
static void gate_pulse_end_cb(void *arg)
{
    gate_pulse_ctx_t *ctx = (gate_pulse_ctx_t *)arg;
    gpio_set_level(ctx->pin, ctx->active_low ? 1 : 0);
    esp_timer_delete(ctx->timer);
    free(ctx);
}

/* Rozpoczyna nieblokujacy impuls: ustawia pin na poziom "aktywny" i planuje jego powrot do poziomu
 * "nieaktywny" po AP_WEBSERVER_GATE_PULSE_MS milisekundach za pomoca jednorazowego timera esp_timer. */
static void start_gate_pulse(gpio_num_t pin, bool active_low)
{
    gpio_set_level(pin, active_low ? 0 : 1);

    gate_pulse_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        gpio_set_level(pin, active_low ? 1 : 0);
        return;
    }
    ctx->pin = pin;
    ctx->active_low = active_low;

    const esp_timer_create_args_t timer_args = {
        .callback = &gate_pulse_end_cb,
        .arg = ctx,
        .name = "gate_pulse",
    };
    if (esp_timer_create(&timer_args, &ctx->timer) == ESP_OK
        && esp_timer_start_once(ctx->timer, (uint64_t)AP_WEBSERVER_GATE_PULSE_MS * 1000ULL) == ESP_OK) {
        return;
    }

    /* Nie udalo sie uruchomic timera - dla bezpieczenstwa nie zostawiaj pinu na stale aktywnego. */
    gpio_set_level(pin, active_low ? 1 : 0);
    free(ctx);
}

/* Wczytuje zapisane wczesniej haslo logowania z NVS (jesli istnieje) do s_login_password. */
static void load_login_password_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(AP_WEBSERVER_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    char stored[65] = {0};
    size_t len = sizeof(stored);
    if (nvs_get_str(handle, AP_WEBSERVER_NVS_KEY_PWD, stored, &len) == ESP_OK && strlen(stored) == 8) {
        strlcpy(s_login_password, stored, sizeof(s_login_password));
        ESP_LOGI(TAG, "Wczytano zapisane haslo logowania z NVS");
    }
    nvs_close(handle);
}

/* Zapisuje nowe haslo logowania w NVS, tak aby przetrwalo restart urzadzenia. */
static esp_err_t save_login_password_to_nvs(const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(AP_WEBSERVER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, AP_WEBSERVER_NVS_KEY_PWD, password);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static const char *LOGIN_PAGE_FMT =
    "<!DOCTYPE html><html lang=\"pl\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Logowanie</title><style>"
    "body{font-family:sans-serif;background:#111;color:#eee;display:flex;"
    "align-items:center;justify-content:center;height:100vh;margin:0}"
    ".box{background:#222;padding:2em;border-radius:12px;text-align:center}"
    "input{padding:0.6em;font-size:1.1em;border-radius:6px;border:none;margin-top:1em;width:100%%;box-sizing:border-box}"
    "button{margin-top:1em;padding:0.6em 1.5em;font-size:1.1em;border-radius:6px;border:none;"
    "background:#2d8cf0;color:#fff;width:100%%}"
    ".err{color:#f66;margin-top:0.5em}"
    "</style></head><body><div class=\"box\">"
    "<h2>ESP32 - Logowanie</h2>"
    "<form method=\"POST\" action=\"/login\">"
    "<input type=\"password\" name=\"password\" placeholder=\"Haslo\" maxlength=\"32\" autofocus required>"
    "<button type=\"submit\">Zaloguj</button>"
    "</form>%s"
    "</div></body></html>";

static esp_err_t send_login_page(httpd_req_t *req, bool show_error)
{
    char page[1024];
    snprintf(page, sizeof(page), LOGIN_PAGE_FMT,
              show_error ? "<div class=\"err\">Nieprawidlowe haslo</div>" : "");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, page);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    if (request_is_authenticated(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/panel");
        return httpd_resp_send(req, NULL, 0);
    }
    return send_login_page(req, false);
}

static esp_err_t login_post_handler(httpd_req_t *req)
{
    char body[128];
    if (read_request_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Bad request");
    }

    char password[65] = {0};
    bool ok = (httpd_query_key_value(body, "password", password, sizeof(password)) == ESP_OK)
              && safe_streq(password, s_login_password);

    if (!ok) {
        /* Krótkie opóźnienie utrudniające automatyczne próby zgadywania hasła. */
        vTaskDelay(pdMS_TO_TICKS(500));
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_login_page(req, true);
    }

    generate_session_token();
    s_session_active = true;

    char cookie[64];
    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly", s_session_token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/panel");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t logout_get_handler(httpd_req_t *req)
{
    s_session_active = false;
    memset(s_session_token, 0, sizeof(s_session_token));
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t redirect_to_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t panel_get_handler(httpd_req_t *req)
{
    if (!request_is_authenticated(req)) {
        return redirect_to_root(req);
    }

    char page[2048];
    size_t off = 0;
    off += snprintf(page + off, sizeof(page) - off,
        "<!DOCTYPE html><html lang=\"pl\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>Panel sterowania</title><style>"
        "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:1em}"
        "h2{text-align:center}"
        ".row{display:flex;align-items:center;justify-content:space-between;"
        "background:#222;padding:1em;border-radius:10px;margin-bottom:0.7em}"
        ".state{font-weight:bold}"
        ".on{color:#4caf50}.off{color:#f66}"
        "button{padding:0.6em 1.2em;font-size:1em;border:none;border-radius:6px;"
        "background:#2d8cf0;color:#fff}"
        "a.logout{display:block;text-align:center;margin-top:1.5em;color:#aaa}"
        "</style></head><body><h2>Panel sterowania</h2>");

    if (s_espnow_info_valid && off < sizeof(page)) {
        char self_mac[18] = {0};
        char peer_mac[18] = {0};
        format_mac(s_espnow_self_mac, self_mac, sizeof(self_mac));
        if (s_espnow_peer_valid) {
            format_mac(s_espnow_peer_mac, peer_mac, sizeof(peer_mac));
        }
        off += snprintf(page + off, sizeof(page) - off,
            "<div class=\"row\"><div><b>ESP-NOW</b> Kanal: %u<br>MAC lokalny: %s<br>Peer: %s</div></div>",
            s_channel, self_mac, s_espnow_peer_valid ? peer_mac : "nie ustawiony");
    }

    if (s_limit_switch_pin != GPIO_NUM_NC && off < sizeof(page)) {
        bool active = gpio_get_level(s_limit_switch_pin) == 0;
        off += snprintf(page + off, sizeof(page) - off,
            "<div class=\"row\"><div>%s</div><span class=\"state %s\">(%s)</span></div>",
            s_limit_switch_name, active ? "on" : "off", active ? "AKTYWNA" : "NIEAKTYWNA");
    }

    for (size_t i = 0; i < s_gpio_count && off < sizeof(page); i++) {
        if (s_gpios[i].mode == AP_WEBSERVER_GPIO_GATE) {
            bool open = s_gate_open[i];
            off += snprintf(page + off, sizeof(page) - off,
                "<div class=\"row\"><div>%s <span class=\"state %s\">(%s)</span></div>"
                "<form method=\"POST\" action=\"/toggle\">"
                "<input type=\"hidden\" name=\"pin\" value=\"%d\">"
                "<button type=\"submit\">%s</button>"
                "</form></div>",
                s_gpios[i].name, open ? "on" : "off", open ? "OTWARTA" : "ZAMKNIETA",
                (int)s_gpios[i].pin, open ? "Zamknij" : "Otworz");
        } else {
            bool on = gpio_is_on(&s_gpios[i]);
            off += snprintf(page + off, sizeof(page) - off,
                "<div class=\"row\"><div>%s <span class=\"state %s\">(%s)</span></div>"
                "<form method=\"POST\" action=\"/toggle\">"
                "<input type=\"hidden\" name=\"pin\" value=\"%d\">"
                "<button type=\"submit\">Przelacz</button>"
                "</form></div>",
                s_gpios[i].name, on ? "on" : "off", on ? "WLACZONE" : "WYLACZONE",
                (int)s_gpios[i].pin);
        }
    }

    off += snprintf(page + off, sizeof(page) - off,
        "<hr style=\"border-color:#333;margin:1.5em 0\">"
        "<h3>Zmiana hasla</h3>"
        "<form method=\"POST\" action=\"/change_password\">"
        "<input type=\"password\" name=\"old_password\" placeholder=\"Aktualne haslo\" maxlength=\"32\" required>"
        "<input type=\"password\" name=\"new_password\" placeholder=\"Nowe haslo (8 znakow)\" maxlength=\"8\" minlength=\"8\" required style=\"margin-top:0.5em\">"
        "<button type=\"submit\" style=\"margin-top:0.5em\">Zmien haslo</button>"
        "</form>");

    off += snprintf(page + off, sizeof(page) - off,
        "<a class=\"logout\" href=\"/logout\">Wyloguj</a></body></html>");

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, page);
}

static esp_err_t toggle_post_handler(httpd_req_t *req)
{
    if (!request_is_authenticated(req)) {
        return redirect_to_root(req);
    }

    char body[32];
    if (read_request_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Bad request");
    }

    char pin_str[8] = {0};
    if (httpd_query_key_value(body, "pin", pin_str, sizeof(pin_str)) == ESP_OK) {
        int pin = atoi(pin_str);
        for (size_t i = 0; i < s_gpio_count; i++) {
            if ((int)s_gpios[i].pin != pin) {
                continue;
            }
            if (s_gpios[i].mode == AP_WEBSERVER_GPIO_GATE) {
                s_gate_open[i] = !s_gate_open[i];
                ESP_LOGI(TAG, "Brama \"%s\" (GPIO%d) -> %s, impuls %d ms",
                         s_gpios[i].name, pin, s_gate_open[i] ? "OTWARTA" : "ZAMKNIETA",
                         AP_WEBSERVER_GATE_PULSE_MS);
                start_gate_pulse(s_gpios[i].pin, s_gpios[i].active_low);
            } else {
                bool on = gpio_is_on(&s_gpios[i]);
                gpio_set_level(s_gpios[i].pin, on ? gpio_off_level(&s_gpios[i]) : gpio_on_level(&s_gpios[i]));
                ESP_LOGI(TAG, "GPIO%d (%s) -> %d", pin, s_gpios[i].name, !on);
            }
            break;
        }
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/panel");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t change_password_post_handler(httpd_req_t *req)
{
    if (!request_is_authenticated(req)) {
        return redirect_to_root(req);
    }

    char body[96];
    if (read_request_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Bad request");
    }

    char old_password[65] = {0};
    char new_password[65] = {0};
    bool have_old = httpd_query_key_value(body, "old_password", old_password, sizeof(old_password)) == ESP_OK;
    bool have_new = httpd_query_key_value(body, "new_password", new_password, sizeof(new_password)) == ESP_OK;

    if (!have_old || !have_new || !safe_streq(old_password, s_login_password)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_sendstr(req,
            "<p>Bledne aktualne haslo.</p><a href=\"/panel\">Wroc</a>");
    }

    if (strlen(new_password) != 8) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_sendstr(req,
            "<p>Nowe haslo musi miec dokladnie 8 znakow.</p><a href=\"/panel\">Wroc</a>");
    }

    strlcpy(s_login_password, new_password, sizeof(s_login_password));
    esp_err_t err = save_login_password_to_nvs(s_login_password);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Nie udalo sie zapisac hasla w NVS: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Haslo logowania zostalo zmienione");

    /* Uniewazniamy biezaca sesje - wymuszamy ponowne logowanie nowym haslem. */
    s_session_active = false;
    memset(s_session_token, 0, sizeof(s_session_token));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
        "<p>Haslo zostalo zmienione. Zaloguj sie ponownie.</p><a href=\"/\">Przejdz do logowania</a>");
}

/* ---------- Inicjalizacja WiFi AP i serwera HTTP ---------- */

static esp_err_t init_wifi_ap(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.ap.ssid, s_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(s_ssid);
    wifi_config.ap.channel = s_channel;
    wifi_config.ap.max_connection = s_max_conn;
    wifi_config.ap.beacon_interval = s_beacon_interval_ms ? s_beacon_interval_ms : 100;

    if (strlen(s_password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strlcpy((char *)wifi_config.ap.password, s_password, sizeof(wifi_config.ap.password));
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (s_tx_power_dbm > 0) {
        /* Jednostka API to 0.25 dBm (np. 20dBm -> 80). */
        esp_err_t tx_err = esp_wifi_set_max_tx_power(s_tx_power_dbm * 4);
        if (tx_err != ESP_OK) {
            ESP_LOGW(TAG, "Nie udalo sie ustawic mocy nadawania: %s", esp_err_to_name(tx_err));
        }
    }

    ESP_LOGI(TAG, "Access Point uruchomiony: SSID=\"%s\", kanal=%d, beacon=%dms",
             s_ssid, s_channel, wifi_config.ap.beacon_interval);
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = AP_WEBSERVER_HTTPD_STACK;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",        .method = HTTP_GET,  .handler = root_get_handler },
        { .uri = "/login",   .method = HTTP_POST, .handler = login_post_handler },
        { .uri = "/logout",  .method = HTTP_GET,  .handler = logout_get_handler },
        { .uri = "/panel",   .method = HTTP_GET,  .handler = panel_get_handler },
        { .uri = "/toggle",  .method = HTTP_POST, .handler = toggle_post_handler },
        { .uri = "/change_password", .method = HTTP_POST, .handler = change_password_post_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uris[i]));
    }

    return ESP_OK;
}

esp_err_t ap_webserver_start(const ap_webserver_config_t *config)
{
    if (!config || !config->ap_ssid || !config->login_password) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(config->login_password) != 8) {
        ESP_LOGE(TAG, "login_password musi miec dokladnie 8 znakow");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->gpio_count > AP_WEBSERVER_MAX_GPIOS) {
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(s_ssid, config->ap_ssid, sizeof(s_ssid));
    strlcpy(s_password, config->ap_password ? config->ap_password : "", sizeof(s_password));
    strlcpy(s_login_password, config->login_password, sizeof(s_login_password));
    load_login_password_from_nvs(); /* nadpisuje login_password jesli uzytkownik wczesniej je zmienil */
    s_channel = config->ap_channel ? config->ap_channel : 1;
    s_max_conn = config->ap_max_conn ? config->ap_max_conn : 4;
    s_tx_power_dbm = config->ap_tx_power_dbm;
    s_beacon_interval_ms = config->ap_beacon_interval_ms;

    s_gpio_count = config->gpio_count;
    for (size_t i = 0; i < s_gpio_count; i++) {
        s_gpios[i] = config->gpios[i];
        s_gate_open[i] = false;
        /* Ustaw bezpieczny (nieaktywny) poziom PRZED przelaczeniem pinu w tryb wyjscia, aby
         * zminimalizowac ryzyko chwilowego, niezamierzonego zalaczenia przekaznika przy starcie. */
        gpio_set_level(s_gpios[i].pin, gpio_off_level(&s_gpios[i]));
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << s_gpios[i].pin,
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(s_gpios[i].pin, gpio_off_level(&s_gpios[i]));
    }

    s_limit_switch_pin = config->limit_switch_pin;
    strlcpy(s_limit_switch_name,
            config->limit_switch_name ? config->limit_switch_name : "Krancowka",
            sizeof(s_limit_switch_name));
    if (s_limit_switch_pin != GPIO_NUM_NC) {
        gpio_config_t limit_conf = {
            .pin_bit_mask = 1ULL << s_limit_switch_pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&limit_conf);
    }

    s_session_active = false;
    memset(s_session_token, 0, sizeof(s_session_token));

    esp_err_t err = init_wifi_ap();
    if (err != ESP_OK) {
        return err;
    }

    return start_http_server();
}

esp_err_t ap_webserver_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    s_session_active = false;
    return esp_wifi_stop();
}

esp_err_t ap_webserver_trigger_gate(void)
{
    for (size_t i = 0; i < s_gpio_count; i++) {
        if (s_gpios[i].mode != AP_WEBSERVER_GPIO_GATE) {
            continue;
        }
        s_gate_open[i] = !s_gate_open[i];
        ESP_LOGI(TAG, "Brama \"%s\" (GPIO%d) -> %s, impuls %d ms [trigger API]",
                 s_gpios[i].name, (int)s_gpios[i].pin, s_gate_open[i] ? "OTWARTA" : "ZAMKNIETA",
                 AP_WEBSERVER_GATE_PULSE_MS);
        start_gate_pulse(s_gpios[i].pin, s_gpios[i].active_low);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

bool ap_webserver_get_gate_state(bool *is_open)
{
    if (!is_open) {
        return false;
    }
    for (size_t i = 0; i < s_gpio_count; i++) {
        if (s_gpios[i].mode == AP_WEBSERVER_GPIO_GATE) {
            *is_open = s_gate_open[i];
            return true;
        }
    }
    return false;
}

bool ap_webserver_get_limit_switch_state(bool *is_active)
{
    if (!is_active || s_limit_switch_pin == GPIO_NUM_NC) {
        return false;
    }
    *is_active = (gpio_get_level(s_limit_switch_pin) == 0);
    return true;
}

void ap_webserver_set_espnow_info(uint8_t channel, const uint8_t self_mac[6], const uint8_t *peer_mac)
{
    (void)channel;
    if (self_mac) {
        memcpy(s_espnow_self_mac, self_mac, sizeof(s_espnow_self_mac));
        s_espnow_info_valid = true;
    }
    if (peer_mac) {
        memcpy(s_espnow_peer_mac, peer_mac, sizeof(s_espnow_peer_mac));
        s_espnow_peer_valid = true;
    } else {
        memset(s_espnow_peer_mac, 0, sizeof(s_espnow_peer_mac));
        s_espnow_peer_valid = false;
    }
}
