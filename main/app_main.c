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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "identity.h"
#include "lcd.h"
#include "mdns_name.h"
#include "ota_rx.h"
#include "portal.h"
#include "sdkconfig.h"
#include "setup_check.h"
#include "status_http.h"
#include "summary.h"
#include "time_sync.h"
#include "udp_log.h"
#include "weather.h"
#include "web_log.h"
#include "web_log_format.h"
#include "wifi_net.h"

static const char *TAG = "main";

#define TICK_US (1000 * 1000)
#define ALIVE_TICKS 10                      // K12 every 10 s (FR-7.29)
#define ATTEMPT_TIMEOUT_US (15 * 1000000LL)  // FSD §5.3: no IP 15 s after the start
#define ANSWER_FLUSH_MS 1000                // T3: let the 200 reach the client
#define CONFIRM_DEADLINE_US (60 * 1000000LL)  // T13: 60 s from reset
#define LEAVE_MS 200                          // a planned restart leaves the AP first
#define TASK_PERIOD_S 600                     // FR-23.2: one reading every 10 min
#define MAIL_WIFI_MS (15 * 1000)              // FR-25.7: no WiFi within 15 s drops the summary
#ifdef TDS3_TEST_HOLD_S
#define HOLD_TIMEOUT_S TDS3_TEST_HOLD_S  // the sleep test builds cut the hold (R-22)
#else
#define HOLD_TIMEOUT_S DL_HOLD_TIMEOUT_S  // FR-24.6
#endif

typedef enum { APP_TICK, APP_CONN, APP_TIME_SYNCED, APP_KEY } app_kind_t;

typedef struct {
    app_kind_t kind;
    dl_conn_event_t ev;
    bool guard;
    pc_creds_t creds;  // DL_EV_CREDS_SUBMITTED
    char ip[16];       // DL_EV_IP
} app_event_t;

static QueueHandle_t queue;
static esp_timer_handle_t tick_timer, retry_timer, attempt_timer, confirm_timer, led_timer;
static const esp_partition_t *downloaded;  // the slot a successful OTA wrote (T11)

static dl_view_t view;
static char softap_ssid[ID_NAME_MAX], hostname[ID_NAME_MAX], ip[16];
static pc_creds_t stored;
_Static_assert(sizeof stored.ssid == WIFI_NET_SSID_MAX + 1 &&
               sizeof stored.password == WIFI_NET_PASSWORD_MAX + 1,
               "portal and wifi_net credentials differ in size");

static const status_http_source_t status_source = {.ip = ip, .hostname = hostname,
                                                   .rssi = wifi_net_rssi,
                                                   .log = web_log_http,
                                                   .extra_html = weather_html};

static dl_wake_t wake;  // how this boot began (FSD §21)
static bool task_ran;
static char reading_line[DL_LINE_MAX];  // screen line 4 (FR-23.12)
static char prompt_line[DL_LINE_MAX];   // screen line 5 (FR-24.10)

static dl_hold_t hold;         // the setup check (FSD §24)
static uint32_t hold_start_s;  // uptime when the hold began
static int64_t hold_start_us;  // the same, for the LED's half periods (FR-24.9)
static uint32_t live_read_s;   // uptime of the last live reading (FR-24.3)
static uint32_t window_start_s;  // uptime the window counts from (FR-24.5)

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

// FR-24.9: every half period of the hold, the LED by the key. A failed write
// only leaves the LED as it was, so it is not reported.
static void on_led(void *arg)
{
    uint32_t ms = (uint32_t)((esp_timer_get_time() - hold_start_us) / 1000);
    (void)lcd_led(dl_led_lit(DL_HOLD_ON, ms));
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

static void on_key(void)
{
    app_event_t e = {.kind = APP_KEY};
    post(&e);
}

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
#include <sys/time.h>

#include "nvs.h"
#include "nvs_flash.h"

// Sleep test build (FSD §21.3): two sleeps per flash, then awake for good, so the
// bench can flash the next build. The bench loses the first second of output after
// each restart, and a reset from the bench clears RTC memory, so the last boots are
// kept in NVS and logged every 10 s while the device is awake.
static RTC_DATA_ATTR uint32_t test_sleeps;  // since the last boot that was not a wake

#define TEST_BOOTS 8

typedef struct {
    uint32_t seq;
    uint8_t reset;         // esp_reset_reason_t
    uint8_t wake;          // dl_wake_t
    int32_t asleep_ms;     // since the previous sleep entry, -1 unless a sleep wake
    uint32_t slept_at_ms;  // uptime at this boot's sleep entry, 0 for none
} test_boot_t;

typedef struct {
    uint32_t seq;          // of the newest boot
    int64_t slept_us;      // system time at the last sleep entry
    test_boot_t boot[TEST_BOOTS];  // ring, by seq % TEST_BOOTS
} test_history_t;

static test_history_t test_hist;

static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void test_save(void)
{
    nvs_handle_t h;
    if (nvs_open("sleeptest", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "hist", &test_hist, sizeof test_hist);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void test_record_boot(dl_wake_t w)
{
    if (nvs_flash_init() != ESP_OK) {
        return;
    }
    nvs_handle_t h;
    size_t len = sizeof test_hist;
    if (nvs_open("sleeptest", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "hist", &test_hist, &len) != ESP_OK || len != sizeof test_hist) {
            memset(&test_hist, 0, sizeof test_hist);
        }
        nvs_close(h);
    }
    esp_reset_reason_t r = esp_reset_reason();
    int64_t ms = r == ESP_RST_DEEPSLEEP && test_hist.slept_us ? (now_us() - test_hist.slept_us) / 1000 : -1;
    test_boot_t *b = &test_hist.boot[++test_hist.seq % TEST_BOOTS];
    *b = (test_boot_t){.seq = test_hist.seq, .reset = (uint8_t)r, .wake = (uint8_t)w,
                       .asleep_ms = ms < 0 || ms > INT32_MAX ? -1 : (int32_t)ms};
    test_save();
}

static void test_mark_sleep(void)
{
    test_hist.boot[test_hist.seq % TEST_BOOTS].slept_at_ms = (uint32_t)(esp_timer_get_time() / 1000);
    test_hist.slept_us = now_us();
    test_save();
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
    for (uint32_t i = 0; i < TEST_BOOTS; i++) {
        const test_boot_t *b = &test_hist.boot[(test_hist.seq + 1 + i) % TEST_BOOTS];
        if (b->seq == 0) {
            continue;
        }
        ESP_LOGI(TAG, "sleeptest: boot seq=%lu reset=%s wake=%s asleep_ms=%ld slept_at_ms=%lu",
                 (unsigned long)b->seq, reset_name((esp_reset_reason_t)b->reset),
                 wake_names[b->wake], (long)b->asleep_ms, (unsigned long)b->slept_at_ms);
    }
}
#endif

// ── Weather region (FSD §23) ─────────────────────────────────────────────

// One reading, logged (L21) and kept, or the failure logged (L22); either way the
// boot carries on (FR-23.4). Line 4 shows the newest kept reading.
static void take_reading(void)
{
    wx_reading_t r;
    esp_err_t err = weather_read(&r);
    if (err == ESP_OK) {
        char iso[WX_ISO_MAX], line[WL_LINE_MAX];
        weather_iso(&r, iso);
        wx_log_line(&r, iso, line, sizeof line);
        web_log_reading(line);
    } else {
        web_log_bme280_failed(err);
    }
    wx_screen_line(weather_newest(), reading_line);
}

// FR-24.3: during the hold, line 4 shows a live reading, neither kept nor logged.
static void show_live_reading(void)
{
    wx_reading_t r;
    wx_screen_line(weather_sample(&r) == ESP_OK ? &r : NULL, reading_line);
}

// ── Summary email region (FSD §25) ───────────────────────────────────────

static esp_err_t wifi_stop_cb(void *arg);

static SemaphoreHandle_t lean_ip;  // given when a timer wake's station gets an address

static void on_lean_net(wifi_net_event_t ev, const char *addr)
{
    if (ev == WIFI_NET_EV_GOT_IP) {
        xSemaphoreGive(lean_ip);
    }
}

// FR-25.6: on a timer wake WiFi is off; bring the station up for the summary
// only. False if it gets no address within 15 s (FR-25.7).
static bool lean_wifi_up(void)
{
    uint8_t mac[6];
    wifi_net_base_mac(mac);
    id_hostname(CONFIG_IDENTITY_HOSTNAME_PREFIX, mac, hostname);
    lean_ip = xSemaphoreCreateBinary();
    if (lean_ip == NULL || wifi_net_init(on_lean_net, hostname) != ESP_OK) {
        return false;
    }
    deep_sleep_on_prepare(wifi_stop_cb, NULL);  // FR-21.7: leave the AP before sleeping
    if (!wifi_net_creds_load(stored.ssid, stored.password) ||
        wifi_net_sta_start(stored.ssid, stored.password) != ESP_OK) {
        return false;
    }
    return xSemaphoreTake(lean_ip, pdMS_TO_TICKS(MAIL_WIFI_MS)) == pdTRUE;
}

// FR-25.3 – FR-25.9: a due summary is sent once, or dropped and logged.
static void send_summary(void)
{
    bool lean = wake == DL_WAKE_TIMER;
    bool up = lean ? lean_wifi_up() : view.conn == DL_ONLINE;
    if (!up) {
        web_log_mail_failed("wifi");  // L26
        return;
    }
    if (!lean) {
        esp_task_wdt_delete(NULL);  // the send blocks this task for up to 30 s (FR-25.7)
    }
    uint32_t count = 0;
    mail_tx_result_t why = MAIL_TX_OK;
    if (summary_send(hostname, &count, &why) == ESP_OK) {
        web_log_mail_sent(summary_recipient(), count);  // L25
    } else {
        web_log_mail_failed(mail_tx_result_name(why));  // L26
    }
    if (!lean) {
        esp_task_wdt_add(NULL);
    }
}

// The task (FR-21.4, FR-21.9): takes a reading (FR-23.1), sends a due summary
// (FR-25.3), then returns the sleep period it asks for (FR-23.2), 0 to stay
// awake. The sleep test build sleeps twice per flash with its own period, then
// stays awake.
static uint32_t run_task(void)
{
    take_reading();
    if (summary_due()) {
        send_summary();
    }
#ifdef TDS3_TEST_SLEEP_S
    return test_sleeps++ < 2 ? TDS3_TEST_SLEEP_S : 0;
#else
    return TASK_PERIOD_S;
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
            redrawn[i] = dl_line_logged(i, shown[i], want[i]);  // FR-7.33
            strcpy(shown[i], want[i]);
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

// ── Setup check region (FSD §24) ─────────────────────────────────────────

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void execute_hold(unsigned a)
{
    uint32_t now = uptime_s();
    if (a & DL_H_ENTER) {
        hold_start_s = live_read_s = now;
        hold_start_us = esp_timer_get_time();
        web_log_setup_pending();  // L23
        esp_timer_start_periodic(led_timer, DL_LED_HALF_MS * 1000);  // FR-24.9
        esp_err_t err = setup_check_key_start(on_key);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            web_log_warn("setup key start", err);
        }
        dl_hold_line(0, HOLD_TIMEOUT_S, prompt_line);
        show_live_reading();
    }
    if (a & DL_H_CLEAR_MARK) {
        esp_err_t err = setup_check_mark(false);
        if (err != ESP_OK) {
            web_log_warn("setup check clear", err);
        }
    }
    if (a & (DL_H_LOG_KEY | DL_H_LOG_TIMEOUT)) {
        web_log_setup_done(a & DL_H_LOG_KEY ? "key" : "timeout");  // L24
        setup_check_key_stop();
        esp_timer_stop(led_timer);
        (void)lcd_led(dl_led_lit(DL_HOLD_NONE, 0));  // lit again with the panel
        prompt_line[0] = '\0';
        wx_screen_line(weather_newest(), reading_line);  // line 4 back to the kept reading
    }
    if (a & DL_H_START_WINDOW) {
        window_start_s = now;
    }
}

static void hold_event(dl_hold_event_t ev, bool guard)
{
    dl_hold_step_t step = dl_hold_step(hold, ev, guard, wake, uptime_s() - hold_start_s,
                                       HOLD_TIMEOUT_S);
    hold = step.next;
    execute_hold(step.actions);
}

// Each tick of the hold, before the draw: the countdown (FR-24.10) and the live
// reading every 5 s (FR-24.3). The timeout (FR-24.6) is checked after the draw,
// so 00:00 is shown.
static void hold_tick(void)
{
    if (hold != DL_HOLD_ON) {
        return;
    }
    uint32_t now = uptime_s();
    dl_hold_line(now - hold_start_s, HOLD_TIMEOUT_S, prompt_line);
    if (now - live_read_s >= DL_HOLD_READ_S) {
        live_read_s = now;
        show_live_reading();
    }
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
        err = summary_store_submitted();  // FR-25.2
        if (err != ESP_OK) {
            web_log_warn("mail settings store", err);
        }
        web_log_provisioned(stored.ssid);
    }
    if (a & DL_C_MARK_SETUP) {
        esp_err_t err = setup_check_mark(true);  // FR-24.1
        if (err != ESP_OK) {
            web_log_warn("setup check mark", err);
        }
    }
    if (a & DL_C_STOP_SOFTAP) {
        vTaskDelay(pdMS_TO_TICKS(ANSWER_FLUSH_MS));
        portal_stop();  // the SoftAP itself goes down with the station start below
    }
    if (a & DL_C_START_SOFTAP) {
        web_log_portal(softap_ssid);
        esp_err_t err = wifi_net_softap_start(softap_ssid);
        if (err == ESP_OK) {
            err = portal_start_extra(on_submit, &summary_portal);  // FR-25.1
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
        if (err == ESP_OK) {
            err = weather_register(status_http_handle(), status_http_note);  // FR-23.13
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
        err = setup_check_mark(false);  // FR-24.1
        if (err != ESP_OK) {
            web_log_warn("setup check clear", err);
        }
        err = summary_erase();  // FR-25.2
        if (err != ESP_OK) {
            web_log_warn("mail settings erase", err);
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
    if (step.actions & DL_C_MARK_SETUP) {
        hold_event(DL_HEV_MARKED, true);
    }
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
    if (dl_task_due(wake, 0, false, wake == DL_WAKE_TIMER, task_ran, DL_HOLD_NONE)) {
        // FR-21.13: a timer wake runs the task with the panel dark and WiFi off,
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
    take_reading();  // FR-23.3: a value for the panel and the page during the window
    view = (dl_view_t){.conn = DL_BOOT, .softap_ssid = softap_ssid,
                       .stored_ssid = stored.ssid, .ip = ip, .hostname = hostname,
                       .reading = reading_line, .prompt = prompt_line};

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
    const esp_timer_create_args_t led_args = {.callback = on_led, .name = "led"};
    esp_timer_create(&led_args, &led_timer);
    if (image_pending_verify()) {
        esp_timer_start_once(confirm_timer, CONFIRM_DEADLINE_US);  // T13
    }

    err = wifi_net_init(on_net, hostname);
    if (err == ESP_OK) {
        deep_sleep_on_prepare(wifi_stop_cb, NULL);
        err = summary_load();  // FR-25.2: NVS is up; timer wakes use this copy
        if (err != ESP_OK) {
            web_log_warn("mail settings load", err);
        }
    } else {
        web_log_warn("network init", err);
    }
    hold_event(DL_HEV_BOOT, setup_check_pending());  // FR-24.2: NVS is up

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
        if (e.kind == APP_KEY) {
            hold_event(DL_HEV_KEY, false);
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
        hold_tick();
        disp = disp_tick(disp);
        hold_event(DL_HEV_TICK, false);
        bool provisioned = view.conn != DL_BOOT && view.conn != DL_PROVISIONING;
        if (!task_ran && dl_task_due(wake, uptime_s() - window_start_s, image_pending_verify(),
                                     provisioned, task_ran, hold)) {
            run_task_and_sleep();  // FR-21.2 – FR-21.4, FR-24.5, FR-24.6: after the window
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
