// Composition root: turns ESP-IDF and interface events into Device Logic events
// (FSD §5), executes the actions the logic returns, and keeps the screen and the
// log in step with the state. The logic decides; this file performs.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "button.h"
#include "device_logic.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "identity.h"
#include "lcd.h"
#include "mdns_name.h"
#include "net.h"
#include "portal.h"
#include "ota_rx.h"
#include "status_http.h"
#include "udp_log.h"
#include "web_log.h"

static const char *TAG = "main";

#define TICK_US (1000 * 1000)
#define ALIVE_TICKS 10                      // K12 every 10 s (FR-7.29)
#define ATTEMPT_TIMEOUT_US (15 * 1000000LL)  // FSD §5.3: no IP 15 s after the start
#define ANSWER_FLUSH_MS 1000                // T3: let the 200 reach the client
#define CONFIRM_DEADLINE_US (60 * 1000000LL)  // T13: 60 s from reset
#define LEAVE_MS 200                          // a planned restart leaves the AP first

typedef enum { APP_TICK, APP_CONN } app_kind_t;

typedef struct {
    app_kind_t kind;
    dl_conn_event_t ev;
    bool guard;
    pc_creds_t creds;  // DL_EV_CREDS_SUBMITTED
    char ip[16];       // DL_EV_IP
} app_event_t;

static QueueHandle_t queue;
static esp_timer_handle_t tick_timer, retry_timer, attempt_timer, confirm_timer;
static const esp_partition_t *downloaded;  // the slot a successful OTA wrote (T11)

static dl_view_t view;
static char softap_ssid[ID_NAME_MAX], hostname[ID_NAME_MAX], ip[16];
static pc_creds_t stored;

static const status_http_source_t status_source = {.ip = ip, .hostname = hostname,
                                                   .rssi = net_rssi};

static char shown[DL_LINES][DL_LINE_MAX];  // what the panel shows now
static bool redrawn[DL_LINES];            // lines drawn on this tick, for L7

static void post(const app_event_t *e)
{
    xQueueSend(queue, e, 0);
}

static void post_conn(dl_conn_event_t ev)
{
    app_event_t e = {.kind = APP_CONN, .ev = ev};
    post(&e);
}

static void on_tick(void *arg)
{
    app_event_t e = {.kind = APP_TICK};
    post(&e);
}

static void on_retry(void *arg)
{
    post_conn(DL_EV_RETRY);
}

static void on_attempt_timeout(void *arg)
{
    post_conn(DL_EV_ATTEMPT_FAILED);
}

static bool image_pending_verify(void)
{
    esp_ota_img_states_t st;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

static void on_net(net_event_t ev, const char *addr)
{
    app_event_t e = {.kind = APP_CONN};
    if (ev == NET_EV_GOT_IP) {
        e.ev = view.conn == DL_ONLINE ? DL_EV_IP_CHANGED : DL_EV_IP;
        strncpy(e.ip, addr, sizeof e.ip - 1);
        e.guard = image_pending_verify();
    } else {
        e.ev = DL_EV_DISCONNECTED;
    }
    post(&e);
}

static void on_confirm_deadline(void *arg)
{
    app_event_t e = {.kind = APP_CONN, .ev = DL_EV_CONFIRM_DEADLINE,
                     .guard = image_pending_verify()};
    post(&e);
}

static void on_ota(bool ok, const esp_partition_t *slot)
{
    downloaded = slot;
    post_conn(ok ? DL_EV_OTA_SUCCEEDED : DL_EV_OTA_FAILED);
}

static void on_button_reset(void)
{
    post_conn(DL_EV_FACTORY_RESET);
}

#ifdef TDS3_TEST_HANG_HTTP
// Hang test build, variant http (FSD §19.3): GET /hang blocks its handler for good,
// watched like every handler (FR-19.2).
static esp_err_t get_hang(httpd_req_t *req)
{
    esp_task_wdt_add(NULL);
    ESP_LOGW(TAG, "hang: http handler blocked");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return ESP_OK;
}
#endif

static void on_submit(const pc_creds_t *c)
{
    app_event_t e = {.kind = APP_CONN, .ev = DL_EV_CREDS_SUBMITTED, .guard = true, .creds = *c};
    post(&e);
}

// ── Display region ───────────────────────────────────────────────────────

// Draw the lines of `want` that differ from `shown`; stop at the first error.
static esp_err_t draw_changed(char want[DL_LINES][DL_LINE_MAX])
{
    for (int i = 0; i < DL_LINES; i++) {
        redrawn[i] = false;
        if (strcmp(want[i], shown[i]) != 0) {
            esp_err_t err = lcd_draw_line(i, want[i]);
            if (err != ESP_OK) {
                return err;
            }
            strcpy(shown[i], want[i]);
            redrawn[i] = true;
        }
    }
    return ESP_OK;
}

static void execute_disp(unsigned actions, esp_err_t err)
{
    if (actions & DL_ACT_BACKLIGHT_ON) {
        lcd_backlight(true);
    }
    if (actions & DL_ACT_LOG_LINES) {
        for (int i = 0; i < DL_LINES; i++) {
            if (redrawn[i]) {
                web_log_lcd_line(i + 1, shown[i]);
            }
        }
    }
    if (actions & DL_ACT_LOG_INIT_FAILED) {
        web_log_lcd_init_failed(err);
    }
    if (actions & DL_ACT_LOG_WRITE_FAILED) {
        web_log_lcd_write_failed(err);
    }
}

static dl_disp_t disp_tick(dl_disp_t disp)
{
    view.requests = status_http_requests();
    char want[DL_LINES][DL_LINE_MAX];
    dl_screen(&view, want);
    bool changed = memcmp(want, shown, sizeof shown) != 0;
    esp_err_t err = ESP_OK;
    if (dl_disp_tick_draws(disp, changed)) {
        err = draw_changed(want);
    }
    dl_disp_step_t step = dl_disp_step(disp, DL_EV_TICK, changed, err == ESP_OK);
    execute_disp(step.actions, err);
    return step.next;
}

// ── Connectivity region ──────────────────────────────────────────────────

static void start_attempt(void)
{
    web_log_sta_mode(stored.ssid);
#ifdef TDS3_TEST_NO_NETWORK
    // No-network test build (FSD §5.6): the attempt is never made.
    esp_err_t err = ESP_OK;
#else
    esp_err_t err = net_sta_start(&stored);
#endif
    if (err != ESP_OK) {
        web_log_warn("station start", err);
    }
    esp_timer_stop(attempt_timer);
    esp_timer_start_once(attempt_timer, ATTEMPT_TIMEOUT_US);
}

static void execute_conn(const app_event_t *e, dl_conn_step_t step)
{
    unsigned a = step.actions;
    if (a & DL_C_STORE_CREDS) {
        stored = e->creds;
        esp_err_t err = net_creds_store(&stored);
        if (err != ESP_OK) {
            web_log_warn("credentials store", err);
        }
        web_log_provisioned(stored.ssid);
    }
    if (a & DL_C_STOP_SOFTAP) {
        vTaskDelay(pdMS_TO_TICKS(ANSWER_FLUSH_MS));
        portal_stop();  // the SoftAP itself goes down with the station start below
    }
    if (a & DL_C_START_SOFTAP) {
        web_log_portal(softap_ssid);
        esp_err_t err = net_softap_start(softap_ssid);
        if (err == ESP_OK) {
            err = portal_start(on_submit);
        }
        if (err != ESP_OK) {
            web_log_warn("portal start", err);
        }
    }
    if (a & DL_C_START_ATTEMPT) {
        start_attempt();
    }
    if (a & DL_C_ARM_RETRY) {
        uint32_t ms = dl_retry_delay_s(step.n) * 1000;
        web_log_retry(ms, step.n);
        esp_timer_stop(retry_timer);
        esp_timer_start_once(retry_timer, (uint64_t)ms * 1000);
    }
    if (a & DL_C_ABORT_OTA) {
        ota_rx_abort();  // FR-15.13
    }
    if (a & DL_C_STOP_SERVICES) {
        status_http_stop();
        udp_log_stop();
        mdns_name_stop();
    }
    if (a & DL_C_START_SERVICES) {
        esp_err_t err = status_http_start(&status_source);
        if (err == ESP_OK) {
            err = ota_rx_register(status_http_handle(), on_ota, status_http_note);
        }
#ifdef TDS3_TEST_HANG_HTTP
        static const httpd_uri_t hang = {.uri = "/hang", .method = HTTP_GET,
                                         .handler = get_hang};
        httpd_register_uri_handler(status_http_handle(), &hang);
#endif
        if (err != ESP_OK) {
            web_log_warn("status server start", err);
        }
        char gw[16];
        net_gateway(gw);
        err = udp_log_start(gw);
        if (err == ESP_OK) {
            web_log_udp_logging(gw, UDP_LOG_PORT);
        } else {
            web_log_warn("udp log start", err);
        }
        err = mdns_name_start(hostname);
        if (err != ESP_OK) {
            web_log_warn("mdns start", err);
        }
    }
    if (a & DL_C_CONFIRM_IMAGE) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            web_log_ota_confirmed();
            esp_timer_stop(confirm_timer);
        }
    }
    if (a & DL_C_SELECT_SLOT) {
        esp_err_t err = downloaded ? esp_ota_set_boot_partition(downloaded) : ESP_FAIL;
        if (err != ESP_OK) {
            web_log_warn("select new slot", err);
        }
    }
    if (a & DL_C_ERASE_CREDS) {
        web_log_factory_reset();
        esp_err_t err = net_creds_erase();
        if (err == ESP_OK) {
            web_log_creds_erased();
        } else {
            web_log_warn("credentials erase", err);
        }
    }
    if (a & DL_C_RESTART) {
        if (e->ev == DL_EV_CONFIRM_DEADLINE) {
            web_log_ota_not_confirmed();
        }
        net_sta_stop();  // leave the AP (deauth) before a planned restart
        vTaskDelay(pdMS_TO_TICKS(LEAVE_MS));
        esp_restart();
    }
}

static void conn_event(const app_event_t *e)
{
    if (e->ev == DL_EV_IP || e->ev == DL_EV_IP_CHANGED || e->ev == DL_EV_DISCONNECTED) {
        esp_timer_stop(attempt_timer);
    }
    if (e->ev == DL_EV_ATTEMPT_FAILED && view.conn == DL_CONNECTING) {
        net_sta_stop();  // a timed-out attempt is abandoned before T6 arms the retry
    }
    static uint32_t n;  // failed attempts since the last ONLINE (FR-5.18)
    dl_conn_t before = view.conn;
    dl_conn_step_t step = dl_conn_step(before, e->ev, e->guard, n);
    n = step.n;
    if (step.next != before) {
        view.conn = step.next;
        web_log_state(dl_conn_name(step.next));
    }
    if (e->ev == DL_EV_IP || e->ev == DL_EV_IP_CHANGED) {
        strncpy(ip, e->ip, sizeof ip - 1);
        if (step.next == DL_ONLINE) {
            web_log_online(ip, net_rssi());
        }
    }
    execute_conn(e, step);
}

void app_main(void)
{
    web_log_boot();
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    if (invalid) {
        web_log_rolled_back(invalid->label);  // L14
    }
    queue = xQueueCreate(16, sizeof(app_event_t));

    uint8_t mac[6];
    net_base_mac(mac);
    id_softap_ssid(mac, softap_ssid);
    id_hostname(mac, hostname);
    view = (dl_view_t){.conn = DL_BOOT, .softap_ssid = softap_ssid,
                       .stored_ssid = stored.ssid, .ip = ip, .hostname = hostname};

    // D1/D2: start, clear, and the first draw of the BOOT screen.
    char want[DL_LINES][DL_LINE_MAX];
    dl_screen(&view, want);
    esp_err_t err = lcd_start();
    if (err == ESP_OK) {
        err = draw_changed(want);
    }
    dl_disp_step_t dstep = dl_disp_step(DL_LCD_START, DL_EV_START_DONE, false, err == ESP_OK);
    execute_disp(dstep.actions, err);
    dl_disp_t disp = dstep.next;

    const esp_timer_create_args_t tick_args = {.callback = on_tick, .name = "tick"};
    const esp_timer_create_args_t retry_args = {.callback = on_retry, .name = "retry"};
    const esp_timer_create_args_t attempt_args = {.callback = on_attempt_timeout,
                                                  .name = "attempt"};
    esp_timer_create(&tick_args, &tick_timer);
    esp_timer_create(&retry_args, &retry_timer);
    esp_timer_create(&attempt_args, &attempt_timer);
    const esp_timer_create_args_t confirm_args = {.callback = on_confirm_deadline,
                                                  .name = "confirm"};
    esp_timer_create(&confirm_args, &confirm_timer);
    if (image_pending_verify()) {
        esp_timer_start_once(confirm_timer, CONFIRM_DEADLINE_US);  // T13
    }

    err = net_init(on_net, hostname);
    if (err != ESP_OK) {
        web_log_warn("network init", err);
    }

    // FR-19.1: this task is watched; the 1 s tick below feeds it.
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "Init complete, running event-driven");

    // T1/T2: credentials read.
    app_event_t cr = {.kind = APP_CONN, .ev = DL_EV_CREDS_READ, .guard = net_creds_load(&stored)};
    conn_event(&cr);
    err = button_start(on_button_reset);  // sampled only after BOOT exits (§18.1)
    if (err != ESP_OK) {
        web_log_warn("button start", err);
    }

    esp_timer_start_periodic(tick_timer, TICK_US);
    for (uint32_t tick = 0, alive = 0;;) {
        app_event_t e;
        if (xQueueReceive(queue, &e, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (e.kind == APP_CONN) {
            conn_event(&e);
            continue;
        }
        esp_task_wdt_reset();
#ifdef TDS3_TEST_HANG_APP
        // Hang test build, variant app (FSD §19.3): block 20 s after startup (FR-19.1).
        if (tick == 20) {
            ESP_LOGW(TAG, "hang: application task blocked");
            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
#endif
        disp = disp_tick(disp);
        if (++tick % ALIVE_TICKS == 0) {
            web_log_alive(alive++);
        }
    }
}
