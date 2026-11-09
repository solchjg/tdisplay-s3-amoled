#include "modem_link.h"

#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sim7670.h"

#define ANSWER_MS 15000  // the supply to the first answer (2.1 s on SLOT2)

static esp_netif_t *ppp;
static SemaphoreHandle_t got_ip;
static bool started;    // sim7670_start done this boot
static bool powered;    // the modem's supply on and the modem answering
static bool searching;  // FR-35.14: a GPS search runs
static bool gnss_on;    // its GNSS is on (a link pauses it)

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

static bool start_once(const ul_modem_t *m)
{
    if (started) {
        return true;
    }
    if (!netif_once()) {
        return false;
    }
    const sim7670_config_t cfg = {.uart_port = BOARD_MODEM_UART,
                                  .rx_gpio = BOARD_MODEM_RX,
                                  .tx_gpio = BOARD_MODEM_TX,
                                  .baud = BOARD_MODEM_BAUD,
                                  .supply_gpio = BOARD_MODEM_SUPPLY,
                                  .dtr_gpio = BOARD_MODEM_DTR};
    started = sim7670_start(&cfg, m->apn, ppp) == ESP_OK;
    return started;
}

// FR-35.23: AT+CPOF, then the supply cut and held low through deep sleep.
static void power_down(void)
{
    if (started) {
        (void)sim7670_power_off();
    }
    powered = false;
    gnss_on = false;
}

// The supply on and the modem answering AT; false when it never answers.
static bool power_up(const ul_modem_t *m)
{
    if (!start_once(m)) {
        return false;
    }
    if (!powered) {
        powered = sim7670_power_on(ANSWER_MS, NULL) == ESP_OK;
    }
    return powered;
}

static void fail(modem_link_t *r, const char *why, int64_t t0)
{
    r->failed = why;
    r->failed_ms = ms_since(t0);
    searching = false;
    power_down();
}

static bool sim_ready(void)
{
    // "SIM busy" for about a second after the first answer (SLOT2).
    for (int i = 0; i < 10; i++) {
        const char *a;
        if (sim7670_at("AT+CPIN?", NULL, 5000, &a) == ESP_OK && strstr(a, "READY") != NULL) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

// FR-35.14: the GNSS off and LTE on again, free for an attach.
static void gnss_pause(void)
{
    if (gnss_on) {
        sim7670_gnss_off();
        sim7670_at("AT+CFUN=1", NULL, 10000, NULL);
        gnss_on = false;
    }
}

bool modem_link_up(const ul_modem_t *m, bool want_time, modem_link_t *r)
{
    memset(r, 0, sizeof *r);
    r->csq = 99;
    int64_t t0 = esp_timer_get_time();
    if (!power_up(m)) {
        fail(r, "power", t0);
        return false;
    }
    gnss_pause();
    int64_t t_on = esp_timer_get_time();
    if (!sim_ready()) {
        fail(r, "sim", t0);
        return false;
    }
    const sim7670_attach_cfg_t ac = {.apn = m->apn, .plmn = m->plmn};
    long reg_ms = -1;
    uint32_t ms = 0;
    long left = UL_REGISTER_MS - ms_since(t0);
    if (left > 0 && sim7670_attach(&ac, (uint32_t)left, &ms) == ESP_OK) {
        reg_ms = ms_since(t0);
        r->attached = true;
        r->attach_ms = ms_since(t_on);
        sim7670_csq(&r->csq);
        sim7670_network(r->net, sizeof r->net);
    }
    if (ul_modem_link(reg_ms, 0) == UL_LINK_REGISTER) {
        fail(r, "register", t0);
        return false;
    }
    int64_t t;
    if (want_time && sim7670_network_time(&t) == ESP_OK) {
        r->net_time = t;  // FR-35.13
    }
    int64_t t_reg = esp_timer_get_time();
    xSemaphoreTake(got_ip, 0);  // nothing left from an earlier link
    long ppp_ms = -1;
    if (sim7670_ppp_up() == ESP_OK && xSemaphoreTake(got_ip, pdMS_TO_TICKS(UL_PPP_MS)) == pdTRUE) {
        ppp_ms = ms_since(t_reg);
    }
    if (ul_modem_link(reg_ms, ppp_ms) != UL_LINK_UP) {
        sim7670_ppp_down();
        fail(r, "ppp", t0);
        return false;
    }
    esp_netif_set_default_netif(ppp);  // the senders' requests go over the modem
    return true;
}

void modem_link_down(void)
{
    if (started) {
        sim7670_ppp_down();
    }
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta != NULL) {
        esp_netif_set_default_netif(sta);
    }
    if (!searching) {
        power_down();  // FR-35.23
    }
}

void modem_link_off(void)
{
    // FR-35.7: the supply driven low and held, without starting the UART. The
    // level is set before the pad is an output and no pull-up is turned on (as
    // a pin reset would), so GPIO 21 never rises over the board's pull-down.
    gpio_set_level(BOARD_MODEM_SUPPLY, 0);
    gpio_config_t io = {.pin_bit_mask = 1ULL << BOARD_MODEM_SUPPLY,
                        .mode = GPIO_MODE_OUTPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&io);
    gpio_hold_dis(BOARD_MODEM_SUPPLY);
    gpio_hold_en(BOARD_MODEM_SUPPLY);
    powered = false;
}

static bool gnss_up(void)
{
    // FR-35.14: LTE stopped for the search.
    gnss_on = sim7670_at("AT+CFUN=0", NULL, 10000, NULL) == ESP_OK && sim7670_gnss_on(false) == ESP_OK;
    return gnss_on;
}

bool modem_link_gnss_start(const ul_modem_t *m)
{
    if (!power_up(m)) {
        power_down();
        return false;
    }
    searching = true;
    return gnss_up();
}

bool modem_link_gnss_read(bool *fix, double *lat, double *lon, int64_t *utc)
{
    *fix = false;
    if (!searching || !powered) {
        return false;
    }
    if (!gnss_on && !gnss_up()) {  // a link paused it
        return false;
    }
    sim7670_gnss_t g;
    if (sim7670_gnss_read(&g) != ESP_OK) {
        return false;
    }
    *fix = g.fix;
    *lat = g.lat;
    *lon = g.lon;
    *utc = g.utc;
    return true;
}

void modem_link_gnss_stop(void)
{
    if (!searching) {
        return;
    }
    searching = false;
    if (gnss_on) {
        sim7670_gnss_off();
    }
    power_down();  // LTE again only with the next send: a fresh attach
}
