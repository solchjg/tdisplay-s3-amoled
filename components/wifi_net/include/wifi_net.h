// Station, SoftAP and stored credentials: the ESP-IDF WiFi, netif and NVS
// foundation (FSD §8). Events arrive through the callback, from the ESP-IDF
// event task. SoftAP channel, client limit and the NVS namespace are Kconfig
// options (menu "WiFi network").
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_NET_SSID_MAX 32
#define WIFI_NET_PASSWORD_MAX 63

typedef enum { WIFI_NET_EV_GOT_IP, WIFI_NET_EV_DISCONNECTED } wifi_net_event_t;
typedef void (*wifi_net_event_cb_t)(wifi_net_event_t ev, const char *ip);

// NVS, netif, the default event loop and WiFi (both interfaces, nothing started).
esp_err_t wifi_net_init(wifi_net_event_cb_t cb, const char *hostname);
void wifi_net_base_mac(uint8_t mac[6]);

// False when nothing (or an empty SSID) is stored.
bool wifi_net_creds_load(char ssid[WIFI_NET_SSID_MAX + 1],
                         char password[WIFI_NET_PASSWORD_MAX + 1]);
esp_err_t wifi_net_creds_store(const char *ssid, const char *password);
esp_err_t wifi_net_creds_erase(void);

esp_err_t wifi_net_softap_start(const char *ssid);  // open network
// Stops any SoftAP and starts one attempt; an empty password joins an open network.
esp_err_t wifi_net_sta_start(const char *ssid, const char *password);
void wifi_net_sta_stop(void);  // abandons the current attempt
int wifi_net_rssi(void);       // 0 when not associated
void wifi_net_gateway(char out[16]);  // the station's default gateway, dotted quad
