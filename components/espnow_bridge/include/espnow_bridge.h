#ifndef ESPNOW_BRIDGE_H
#define ESPNOW_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t channel;
    bool has_peer;
    uint8_t peer_mac[6];
    uint32_t status_interval_ms;
} espnow_bridge_config_t;

esp_err_t espnow_bridge_start(const espnow_bridge_config_t *config);
esp_err_t espnow_bridge_stop(void);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_BRIDGE_H
