// Commissioning aid, not product code: proves this board's radio associates
// with the bench AP (capability dut-wifi-slot4). Joins the fixed commissioning
// network, then prints its IP, RSSI and a heartbeat every 2 s. Every
// disconnect is printed with its reason and retried at once.

#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// The bench AP is started with these for the probe only; the network is the
// isolated bench network, so they protect nothing.
#define PROBE_SSID "tds3-commission"
#define PROBE_PASS "commission-0441"

static char ip[16] = "none";

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        printf("wifiprobe: connecting to '%s'\n", PROBE_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        printf("wifiprobe: disconnected reason=%d\n", d->reason);
        strcpy(ip, "none");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(ip, sizeof ip, IPSTR, IP2STR(&e->ip_info.ip));
        printf("wifiprobe: got ip %s\n", ip);
    }
}

void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    printf("wifiprobe: nvs=%s\n", esp_err_to_name(e));
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    printf("wifiprobe: init=%s\n", esp_err_to_name(esp_wifi_init(&init)));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    wifi_config_t cfg = {0};
    strcpy((char *)cfg.sta.ssid, PROBE_SSID);
    strcpy((char *)cfg.sta.password, PROBE_PASS);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    printf("wifiprobe: start=%s\n", esp_err_to_name(esp_wifi_start()));

    for (int n = 0;; n++) {
        wifi_ap_record_t ap;
        int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
        printf("wifiprobe: alive %d ip=%s rssi=%d\n", n, ip, rssi);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
