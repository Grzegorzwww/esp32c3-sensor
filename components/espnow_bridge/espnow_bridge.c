#include <string.h>
#include <inttypes.h>

#include "espnow_bridge.h"
#include "ap_webserver.h"

#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "espnow_bridge";

#define ESPNOW_BRIDGE_MAGIC      0xB0A7
#define ESPNOW_BRIDGE_VERSION    1
#define ESPNOW_BRIDGE_CMD_TRIGGER_GATE 1

typedef enum {
    ESPNOW_MSG_STATUS = 1,
    ESPNOW_MSG_CMD = 2,
} espnow_msg_type_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint32_t seq;
    uint8_t cmd;
    uint8_t gate_open;
    uint8_t limit_active;
    uint8_t reserved;
    uint32_t uptime_s;
} espnow_bridge_packet_t;

typedef enum {
    ESPNOW_EVENT_TRIGGER_GATE = 1,
} espnow_event_t;

static espnow_bridge_config_t s_cfg;
static bool s_started = false;
static TaskHandle_t s_task_handle = NULL;
static QueueHandle_t s_evt_queue = NULL;
static uint32_t s_seq = 0;

static void send_status_packet(void)
{
    if (!s_cfg.has_peer) {
        return;
    }

    bool gate_open = false;
    bool limit_active = false;
    bool have_gate = ap_webserver_get_gate_state(&gate_open);
    bool have_limit = ap_webserver_get_limit_switch_state(&limit_active);

    espnow_bridge_packet_t pkt = {
        .magic = ESPNOW_BRIDGE_MAGIC,
        .version = ESPNOW_BRIDGE_VERSION,
        .type = ESPNOW_MSG_STATUS,
        .seq = ++s_seq,
        .cmd = 0,
        .gate_open = have_gate ? (gate_open ? 1 : 0) : 255,
        .limit_active = have_limit ? (limit_active ? 1 : 0) : 255,
        .reserved = 0,
        .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL),
    };

    esp_err_t err = esp_now_send(s_cfg.peer_mac, (const uint8_t *)&pkt, sizeof(pkt));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send status failed: %s", esp_err_to_name(err));
    }
}

static void on_espnow_recv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    if (!esp_now_info || !data || data_len < (int)sizeof(espnow_bridge_packet_t)) {
        return;
    }

    const espnow_bridge_packet_t *pkt = (const espnow_bridge_packet_t *)data;
    if (pkt->magic != ESPNOW_BRIDGE_MAGIC || pkt->version != ESPNOW_BRIDGE_VERSION) {
        return;
    }

    if (pkt->type == ESPNOW_MSG_CMD && pkt->cmd == ESPNOW_BRIDGE_CMD_TRIGGER_GATE) {
        espnow_event_t ev = ESPNOW_EVENT_TRIGGER_GATE;
        if (s_evt_queue) {
            xQueueSend(s_evt_queue, &ev, 0);
        }
    }
}

static void espnow_bridge_task(void *arg)
{
    (void)arg;

    TickType_t wait_ticks = pdMS_TO_TICKS(s_cfg.status_interval_ms ? s_cfg.status_interval_ms : 5000);

    while (s_started) {
        espnow_event_t ev;
        if (xQueueReceive(s_evt_queue, &ev, wait_ticks) == pdTRUE) {
            if (ev == ESPNOW_EVENT_TRIGGER_GATE) {
                esp_err_t err = ap_webserver_trigger_gate();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Trigger bramy z ESPNOW nieudany: %s", esp_err_to_name(err));
                } else {
                    ESP_LOGI(TAG, "Trigger bramy z ESPNOW wykonany");
                }
            }
        } else {
            send_status_packet();
        }
    }

    vTaskDelete(NULL);
}

esp_err_t espnow_bridge_start(const espnow_bridge_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.channel = config->channel;
    s_cfg.has_peer = config->has_peer;
    s_cfg.status_interval_ms = config->status_interval_ms ? config->status_interval_ms : 5000;
    if (s_cfg.has_peer) {
        memcpy(s_cfg.peer_mac, config->peer_mac, sizeof(s_cfg.peer_mac));
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_espnow_recv));

    if (s_cfg.has_peer) {
        esp_now_peer_info_t peer = {0};
        memcpy(peer.peer_addr, s_cfg.peer_mac, sizeof(peer.peer_addr));
        peer.channel = s_cfg.channel;
        peer.ifidx = WIFI_IF_AP;
        peer.encrypt = false;
        ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    }

    uint8_t self_mac[6] = {0};
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, self_mac));
    ap_webserver_set_espnow_info(s_cfg.channel, self_mac, s_cfg.has_peer ? s_cfg.peer_mac : NULL);

    s_evt_queue = xQueueCreate(8, sizeof(espnow_event_t));
    if (!s_evt_queue) {
        esp_now_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    xTaskCreate(espnow_bridge_task, "espnow_bridge", 4096, NULL, 5, &s_task_handle);

    ESP_LOGI(TAG, "ESP-NOW start: channel=%u, peer=%s", s_cfg.channel, s_cfg.has_peer ? "configured" : "none");
    return ESP_OK;
}

esp_err_t espnow_bridge_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    s_started = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(20));
        s_task_handle = NULL;
    }
    if (s_evt_queue) {
        vQueueDelete(s_evt_queue);
        s_evt_queue = NULL;
    }

    esp_now_unregister_recv_cb();
    esp_now_deinit();

    return ESP_OK;
}
