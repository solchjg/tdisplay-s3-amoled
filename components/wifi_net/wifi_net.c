#include "wifi_net.h"

#include <string.h>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_try.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

// Every call reports its error (ESP_TRY), never aborts: a restart outside T10,
// T11, T13 and the watchdog is prohibited (FSD §5.3).

#define NVS_NS CONFIG_WIFI_NET_NVS_NAMESPACE

static wifi_net_event_cb_t event_cb;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        event_cb(WIFI_NET_EV_DISCONNECTED, NULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        char ip[16];
        esp_ip4addr_ntoa(&((ip_event_got_ip_t *)data)->ip_info.ip, ip, sizeof ip);
        event_cb(WIFI_NET_EV_GOT_IP, ip);
    }
}

esp_err_t wifi_net_init(wifi_net_event_cb_t cb, const char *hostname)
{
    event_cb = cb;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    ESP_TRY(esp_netif_init());
    ESP_TRY(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, hostname);  // FR-17.2
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_TRY(esp_wifi_init(&init));
    ESP_TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));  // credentials live in NVS_NS
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    return ESP_OK;
}

void wifi_net_base_mac(uint8_t mac[6])
{
    esp_efuse_mac_get_default(mac);
}

bool wifi_net_creds_load(char ssid[WIFI_NET_SSID_MAX + 1],
                         char password[WIFI_NET_PASSWORD_MAX + 1])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t sl = WIFI_NET_SSID_MAX + 1, pl = WIFI_NET_PASSWORD_MAX + 1;
    bool ok = nvs_get_str(h, "ssid", ssid, &sl) == ESP_OK &&
              nvs_get_str(h, "password", password, &pl) == ESP_OK && ssid[0];
    nvs_close(h);
    return ok;
}

esp_err_t wifi_net_creds_store(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "password", password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t wifi_net_creds_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t wifi_net_softap_start(const char *ssid)
{
    wifi_config_t cfg = {.ap = {.channel = CONFIG_WIFI_NET_SOFTAP_CHANNEL,  // FR-13.1
                                .max_connection = CONFIG_WIFI_NET_SOFTAP_MAX_CLIENTS,
                                .authmode = WIFI_AUTH_OPEN}};
    strncpy((char *)cfg.ap.ssid, ssid, sizeof cfg.ap.ssid);
    cfg.ap.ssid_len = (uint8_t)strlen(ssid);
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    return err;
}

esp_err_t wifi_net_sta_start(const char *ssid, const char *password)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof cfg.sta.ssid);
    strncpy((char *)cfg.sta.password, password, sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);  // also takes the SoftAP down
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    return err;
}

void wifi_net_sta_stop(void)
{
    esp_wifi_disconnect();
}

void wifi_net_gateway(char out[16])
{
    esp_netif_ip_info_t info = {0};
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &info);
    esp_ip4addr_ntoa(&info.gw, out, 16);
}

int wifi_net_rssi(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}
