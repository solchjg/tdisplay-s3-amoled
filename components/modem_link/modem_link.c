#include "modem_link.h"

#include <string.h>

#include "axp2101.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sim7080.h"
#include "tsim7080g_s3.h"

#define PMU_PORT 1           // §35.2: taken in turn with the loop bus
#define PMU_CLK_HZ 400000
#define ASK_PSM_MS 200       // a modem in PSM never answers before the pulse
#define ASK_ON_MS 1500       // a modem that is on must not be pulsed off
#define ANSWER_MS 30000      // after the pulse
#define RESUME_MS 5000       // the kept registration shows a moment after AT
#define PSM_TAU_S 3600       // asked for; the network decides (FR-35.1)
#define PSM_ACTIVE_S 10

static RTC_DATA_ATTR bool in_psm;  // left in PSM by the last run

static esp_netif_t *ppp;
static SemaphoreHandle_t got_ip;
static bool started;  // sim7080_start done this boot
static bool keep_psm;

static long ms_since(int64_t t0)
{
    return (long)((esp_timer_get_time() - t0) / 1000);
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_PPP_GOT_IP) {
        xSemaphoreGive(got_ip);
    }
}

static bool pmu(void)
{
    const axp2101_config_t cfg = {.i2c_port = PMU_PORT,
                                  .sda_gpio = TSIM7080G_S3_PMU_SDA,
                                  .scl_gpio = TSIM7080G_S3_PMU_SCL,
                                  .clk_hz = PMU_CLK_HZ,
                                  .internal_pullup = true};
    return axp2101_start(&cfg) == ESP_OK;
}

static void rails(bool modem_on)
{
    // BLDO1 feeds the level shifter between the ESP32-S3 and the modem: never off.
    axp2101_rail(AXP2101_BLDO1, TSIM7080G_S3_RAIL_SHIFTER_BLDO1_MV, true);
    axp2101_rail(AXP2101_DC3, TSIM7080G_S3_RAIL_MODEM_DC3_MV, modem_on);
}

void modem_link_off(void)
{
    if (pmu()) {
        rails(false);
        axp2101_rail(AXP2101_BLDO2, 0, false);  // the GPS antenna
        axp2101_stop();
    }
    in_psm = false;
}

static bool netif_once(void)
{
    if (ppp) {
        return true;
    }
    got_ip = xSemaphoreCreateBinary();
    esp_netif_init();
    esp_err_t err = esp_event_loop_create_default();
    if (got_ip == NULL || (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, on_ip, NULL) != ESP_OK) {
        return false;
    }
    esp_netif_config_t nc = ESP_NETIF_DEFAULT_PPP();
    ppp = esp_netif_new(&nc);
    return ppp != NULL;
}

// The modem off after a failure: the next send attaches cold (FR-35.9).
static void fail(modem_link_t *r, const char *why, int64_t t0)
{
    r->failed = why;
    r->failed_ms = ms_since(t0);
    if (started) {
        sim7080_power_off();
        sim7080_sleep_hold();
    }
    rails(false);
    axp2101_stop();
    in_psm = false;
}

static bool sim_ready(void)
{
    const char *a;
    return sim7080_at("AT+CPIN?", NULL, 5000, &a) == ESP_OK && strstr(a, "READY") != NULL;
}

bool modem_link_up(const ul_modem_t *m, modem_link_t *r)
{
    memset(r, 0, sizeof *r);
    r->csq = 99;
    r->active_s = r->tau_s = SIM7080_GRANT_NONE;
    int64_t t0 = esp_timer_get_time();
    if (!pmu()) {
        r->failed = "power";
        r->failed_ms = ms_since(t0);
        return false;
    }
    rails(true);
    if (!netif_once()) {
        fail(r, "power", t0);
        return false;
    }
    if (!started) {
        const sim7080_config_t cfg = {.uart_port = 1,
                                      .rx_gpio = TSIM7080G_S3_MODEM_RX,
                                      .tx_gpio = TSIM7080G_S3_MODEM_TX,
                                      .baud = TSIM7080G_S3_MODEM_BAUD,
                                      .pwrkey_gpio = TSIM7080G_S3_MODEM_PWRKEY,
                                      .dtr_gpio = TSIM7080G_S3_MODEM_DTR};
        if (sim7080_start(&cfg, m->apn, ppp) != ESP_OK) {
            fail(r, "power", t0);
            return false;
        }
        started = true;
    }
    sim7080_wake_t w;
    if (sim7080_wake(in_psm ? ASK_PSM_MS : ASK_ON_MS, ANSWER_MS, &w) != ESP_OK) {
        fail(r, "power", t0);
        return false;
    }
    int64_t t_woken = esp_timer_get_time();
    long reg_ms = -1;
    uint32_t ms = 0;
    if (in_psm && sim7080_resume(RESUME_MS, &ms) == ESP_OK) {
        reg_ms = ms_since(t_woken);
        keep_psm = true;
    } else {
        if (!sim_ready()) {
            fail(r, "sim", t0);
            return false;
        }
        const sim7080_attach_cfg_t ac = {.apn = m->apn,
                                         .plmn = m->plmn,
                                         .rat = SIM7080_RAT_CATM,
                                         .psm = true,
                                         .psm_tau_s = PSM_TAU_S,
                                         .psm_active_s = PSM_ACTIVE_S};
        sim7080_cereg_t grant;
        long left = UL_REGISTER_MS - ms_since(t_woken);
        if (left > 0 && sim7080_attach(&ac, (uint32_t)left, &grant, &ms) == ESP_OK) {
            reg_ms = ms_since(t_woken);
            r->attached = true;
            r->attach_ms = (long)ms;
            r->active_s = grant.active_s;
            r->tau_s = grant.tau_s;
            keep_psm = sim7080_psm_keep(&grant, UL_PSM_ACTIVE_MAX_S);
            r->psm = keep_psm;
            sim7080_csq(&r->csq);
            sim7080_network(r->net, sizeof r->net);
        }
    }
    if (ul_modem_link(reg_ms, 0) == UL_LINK_REGISTER) {
        fail(r, "register", t0);
        return false;
    }
    int64_t t_reg = esp_timer_get_time();
    xSemaphoreTake(got_ip, 0);  // nothing left from an earlier link
    long ppp_ms = -1;
    if (sim7080_ppp_up() == ESP_OK && xSemaphoreTake(got_ip, pdMS_TO_TICKS(UL_PPP_MS)) == pdTRUE) {
        ppp_ms = ms_since(t_reg);
    }
    if (ul_modem_link(reg_ms, ppp_ms) != UL_LINK_UP) {
        sim7080_ppp_down();
        fail(r, "ppp", t0);
        return false;
    }
    esp_netif_set_default_netif(ppp);  // the senders' requests go over the modem
    return true;
}

void modem_link_down(void)
{
    sim7080_ppp_down();
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta != NULL) {
        esp_netif_set_default_netif(sta);
    }
    if (keep_psm) {
        in_psm = true;  // FR-35.1: resumes without a new attach next time
    } else {
        sim7080_power_off();
        rails(false);
        in_psm = false;
    }
    sim7080_sleep_hold();
    axp2101_stop();  // the controller back for the loop bus
}
