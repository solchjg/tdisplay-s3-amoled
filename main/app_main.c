// Composition root: turns ESP-IDF and interface events into Device Logic events
// (FSD §5), executes the actions the logic returns, and keeps the screen and the
// log in step with the state. The logic decides; this file performs.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "button.h"
#include "deep_sleep.h"
#include "device_logic.h"
#include "esp_attr.h"
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
#include "ota_rx.h"
#include "portal.h"
#include "sdkconfig.h"
#include "status_http.h"
#include "time_sync.h"
#include "udp_log.h"
#include "web_log.h"
#include "wifi_net.h"

static const char *TAG = "main";

#define TICK_US (1000 * 1000)
#define ALIVE_TICKS 10                      // K12 every 10 s (FR-7.29)
#define ATTEMPT_TIMEOUT_US (15 * 1000000LL)  // FSD §5.3: no IP 15 s after the start
#define ANSWER_FLUSH_MS 1000                // T3: let the 200 reach the client
#define CONFIRM_DEADLINE_US (60 * 1000000LL)  // T13: 60 s from reset
#define LEAVE_MS 200                          // a planned restart leaves the AP first

typedef enum { APP_TICK, APP_CONN, APP_TIME_SYNCED } app_kind_t;

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
_Static_assert(sizeof stored.ssid == WIFI_NET_SSID_MAX + 1 &&
               sizeof stored.password == WIFI_NET_PASSWORD_MAX + 1,
               "portal and wifi_net credentials differ in size");

static const status_http_source_t status_source = {.ip = ip, .hostname = hostname,
                                                   .rssi = wifi_net_rssi,
                                                   .log = web_log_http};

static dl_wake_t wake;  // how this boot began (FSD §21)
static bool task_ran;

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

static void on_net(wifi_net_event_t ev, const char *addr)
{
    app_event_t e = {.kind = APP_CONN};
    if (ev == WIFI_NET_EV_GOT_IP) {
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

static void on_ota(const ota_rx_result_t *r)
{
    if (r->ok) {
        web_log_ota_downloaded(r->bytes);
        web_log_ota_succeeded();
    } else {
        web_log_ota_failed(r->reason);
    }
    downloaded = r->slot;
    post_conn(r->ok ? DL_EV_OTA_SUCCEEDED : DL_EV_OTA_FAILED);
}

static const ota_rx_hooks_t ota_hooks = {.on_result = on_ota,
                                         .on_refused = web_log_ota_refused,
                                         .note = status_http_note};

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

// ── Sleep region (FSD §21) ───────────────────────────────────────────────

static dl_wake_t boot_wake(void)
{
    switch (deep_sleep_wake_cause()) {
    case DEEP_SLEEP_WAKE_TIMER:
        return DL_WAKE_TIMER;
    case DEEP_SLEEP_WAKE_BUTTON:
        return DL_WAKE_KEY;
    default:
        return DL_WAKE_NONE;
    }
}

#ifdef TDS3_TEST_SLEEP_S
// Sleep test build (FSD §21.3): two sleeps per flash, then awake for good, so the
// bench can flash the next build. The bench loses the first second of output after
// each restart, so every boot since the flash is kept in RTC memory (through sleeps
// and any reset taken while asleep) and logged again once the device stays awake.
#include <sys/time.h>

#define TEST_BOOTS 8
#define TEST_MAGIC 0x534c5048u

typedef struct {
    uint32_t magic;
    uint32_t sleeps;   // sleeps asked for since the flash
    uint32_t boots;
    bool asleep;       // set at sleep entry, cleared by the next boot
    int64_t slept_us;  // system time at the last sleep entry
    struct {
        esp_reset_reason_t reset;
        dl_wake_t wake;
        int32_t asleep_ms;    // since the previous sleep entry, -1 for none
        uint32_t slept_at_ms; // uptime at this boot's sleep entry, 0 for none
    } boot[TEST_BOOTS];
} test_history_t;

static RTC_NOINIT_ATTR test_history_t test_hist;

static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// A boot that does not follow a sleep (the flash's reset) starts a new history.
static void test_record_boot(dl_wake_t w)
{
    esp_reset_reason_t r = esp_reset_reason();
    bool after_sleep = test_hist.magic == TEST_MAGIC &&
                       (r == ESP_RST_DEEPSLEEP || test_hist.asleep);
    if (!after_sleep) {
        memset(&test_hist, 0, sizeof test_hist);
        test_hist.magic = TEST_MAGIC;
    }
    int64_t ms = after_sleep ? (now_us() - test_hist.slept_us) / 1000 : -1;
    test_hist.asleep = false;
    if (test_hist.boots < TEST_BOOTS) {
        test_hist.boot[test_hist.boots].reset = r;
        test_hist.boot[test_hist.boots].wake = w;
        test_hist.boot[test_hist.boots].asleep_ms = ms < 0 || ms > INT32_MAX ? -1 : (int32_t)ms;
        test_hist.boot[test_hist.boots].slept_at_ms = 0;
    }
    test_hist.boots++;
}

static void test_mark_sleep(void)
{
    if (test_hist.boots - 1 < TEST_BOOTS) {
        test_hist.boot[test_hist.boots - 1].slept_at_ms =
            (uint32_t)(esp_timer_get_time() / 1000);
    }
    test_hist.slept_us = now_us();
    test_hist.asleep = true;
}

static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "sw";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other";
    }
}

static void test_log_history(void)
{
    static const char *wake_names[] = {"none", "timer", "button"};
    uint32_t n = test_hist.boots < TEST_BOOTS ? test_hist.boots : TEST_BOOTS;
    for (uint32_t i = 0; i < n; i++) {
        ESP_LOGI(TAG, "sleeptest: boot %lu/%lu reset=%s wake=%s asleep_ms=%ld slept_at_ms=%lu",
                 (unsigned long)i, (unsigned long)test_hist.boots,
                 reset_name(test_hist.boot[i].reset), wake_names[test_hist.boot[i].wake],
                 (long)test_hist.boot[i].asleep_ms, (unsigned long)test_hist.boot[i].slept_at_ms);
    }
}
#endif

// The task (FR-21.4, FR-21.9): reads the sensors and acts on the data, then
// returns the sleep period it asks for, 0 to stay awake. The shipped image has
// no task yet, so it never sleeps; the sleep test build stands in for one.
static uint32_t run_task(void)
{
#ifdef TDS3_TEST_SLEEP_S
    return test_hist.sleeps++ < 2 ? TDS3_TEST_SLEEP_S : 0;
#else
    return 0;
#endif
}

static void run_task_and_sleep(void)
{
    task_ran = true;
    uint32_t s = run_task();
    if (s == 0) {
        return;
    }
    web_log_sleep(s);                    // L18
#ifdef TDS3_TEST_SLEEP_S
    test_mark_sleep();
#endif
    esp_err_t err = deep_sleep_for(s);   // returns only on failure
    web_log_warn("deep sleep", err);
}

// From the SNTP (lwIP) task: logged by the application task, whose stack has room.
static void on_time_synced(void)
{
    app_event_t e = {.kind = APP_TIME_SYNCED};
    post(&e);
}

static esp_err_t lcd_sleep_cb(void *arg)
{
    return lcd_sleep();  // FR-21.6
}

static esp_err_t wifi_stop_cb(void *arg)
{
    return wifi_net_stop();  // FR-21.7: a station leaves its AP
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
    esp_err_t err = wifi_net_sta_start(stored.ssid, stored.password);
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
        esp_err_t err = wifi_net_creds_store(stored.ssid, stored.password);
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
        esp_err_t err = wifi_net_softap_start(softap_ssid);
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
        time_sync_stop();
        status_http_stop();
        udp_log_stop();
        mdns_name_stop();
    }
    if (a & DL_C_START_SERVICES) {
        esp_err_t err = status_http_start(&status_source);
        if (err == ESP_OK) {
            err = ota_rx_register(status_http_handle(), &ota_hooks);
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
        wifi_net_gateway(gw);
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
        err = time_sync_start(on_time_synced);  // FR-22.1: the network is up
        if (err != ESP_OK) {
            web_log_warn("time sync start", err);
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
        esp_err_t err = wifi_net_creds_erase();
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
        wifi_net_sta_stop();  // leave the AP (deauth) before a planned restart
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
        wifi_net_sta_stop();  // a timed-out attempt is abandoned before T6 arms the retry
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
            web_log_online(ip, wifi_net_rssi());
        }
    }
    execute_conn(e, step);
}

void app_main(void)
{
    web_log_boot();
    time_sync_init();  // FR-22.2, FR-22.4: zone, and whether a sync before a sleep holds
    wake = boot_wake();
#ifdef TDS3_TEST_SLEEP_S
    test_record_boot(wake);
#endif
    if (wake != DL_WAKE_NONE) {
        web_log_wake(wake == DL_WAKE_TIMER ? "timer" : "button");  // L19
    }
    deep_sleep_on_prepare(lcd_sleep_cb, NULL);
    // A timer wake follows a sleep, which only a provisioned device takes (FR-21.12).
    if (dl_task_due(wake, 0, false, wake == DL_WAKE_TIMER, task_ran)) {
        // FR-21.10: a timer wake runs the task with the panel dark and WiFi off,
        // then sleeps again. Only a task that asks to stay awake gets past here.
        run_task_and_sleep();
    }
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    if (invalid) {
        web_log_rolled_back(invalid->label);  // L14
    }
    queue = xQueueCreate(16, sizeof(app_event_t));

    uint8_t mac[6];
    wifi_net_base_mac(mac);
    id_softap_ssid(CONFIG_IDENTITY_SOFTAP_SSID_PREFIX, mac, softap_ssid);
    id_hostname(CONFIG_IDENTITY_HOSTNAME_PREFIX, mac, hostname);
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

    err = wifi_net_init(on_net, hostname);
    if (err == ESP_OK) {
        deep_sleep_on_prepare(wifi_stop_cb, NULL);
    } else {
        web_log_warn("network init", err);
    }

    // FR-19.1: this task is watched; the 1 s tick below feeds it.
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "Init complete, running event-driven");

    // T1/T2: credentials read.
    app_event_t cr = {.kind = APP_CONN, .ev = DL_EV_CREDS_READ, .guard = wifi_net_creds_load(stored.ssid, stored.password)};
    conn_event(&cr);
    err = button_start(web_log_button_held, on_button_reset);  // sampled only after BOOT exits (§18.1)
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
        if (e.kind == APP_TIME_SYNCED) {
            web_log_time_synced();  // L20 (FR-22.5)
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
        uint32_t up_s = (uint32_t)(esp_timer_get_time() / 1000000);
        bool provisioned = view.conn != DL_BOOT && view.conn != DL_PROVISIONING;
        if (!task_ran &&
            dl_task_due(wake, up_s, image_pending_verify(), provisioned, task_ran)) {
            run_task_and_sleep();  // FR-21.2 – FR-21.4: after the window
        }
        if (++tick % ALIVE_TICKS == 0) {
            web_log_alive(alive++);
        }
#ifdef TDS3_TEST_SLEEP_S
        if (tick % ALIVE_TICKS == 5) {
            test_log_history();
        }
#endif
    }
}
