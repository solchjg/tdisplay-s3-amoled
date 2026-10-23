// Station, SoftAP and stored credentials: the ESP-IDF WiFi, netif and NVS
// foundation (FSD §8) as the composition root uses it. Events arrive through
// the callback, from the ESP-IDF event task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "portal_core.h"

typedef enum { NET_EV_GOT_IP, NET_EV_DISCONNECTED } net_event_t;
typedef void (*net_event_cb_t)(net_event_t ev, const char *ip);

esp_err_t net_init(net_event_cb_t cb, const char *hostname);
void net_base_mac(uint8_t mac[6]);

bool net_creds_load(pc_creds_t *out);
esp_err_t net_creds_store(const pc_creds_t *c);
esp_err_t net_creds_erase(void);

esp_err_t net_softap_start(const char *ssid);
esp_err_t net_sta_start(const pc_creds_t *c);  // stops any SoftAP, starts one attempt
void net_sta_stop(void);                       // abandons the current attempt
int net_rssi(void);
