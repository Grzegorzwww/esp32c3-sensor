#include "mqtt_prefix.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT_PREFIX";

static char s_prefix[64] = "user";   // fallback gdy brak NVS
static char s_buf[160];              // bufor na złożony temat

const char *mqtt_topic(const char *subtopic)
{
    snprintf(s_buf, sizeof(s_buf), "%s/%s", s_prefix, subtopic);
    return s_buf;
}

void mqtt_prefix_load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("wifi_config", NVS_READONLY, &h) == ESP_OK) {
        char raw[64] = "user";
        size_t len = sizeof(raw);
        if (nvs_get_str(h, "mqtt_email", raw, &len) != ESP_OK) {
            strncpy(raw, "user", sizeof(raw));
        }
        nvs_close(h);

        // Zamień '@' na '_at_' — unikamy znaków specjalnych w MQTT topic
        // np. jan@gmail.com  →  jan_at_gmail.com
        char *at = strchr(raw, '@');
        if (at != NULL) {
            size_t before = (size_t)(at - raw);
            snprintf(s_prefix, sizeof(s_prefix), "%.*s_at_%s",
                     (int)before, raw, at + 1);
        } else {
            strncpy(s_prefix, raw, sizeof(s_prefix));
        }
        s_prefix[sizeof(s_prefix) - 1] = '\0';
    }
    ESP_LOGI(TAG, "📧 MQTT prefix: %s", s_prefix);
}
