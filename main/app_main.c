// Composition root: turns ESP-IDF and interface events into Device Logic events
// (FSD §5), executes the actions the logic returns, and keeps the screen and the
// log in step with the state. The logic decides; this file performs.
//
// How it runs. app_main() does the boot work in order (wake cause, Notecard,
// tests a reset cut short, the task of a timer wake, the history, the first
// reading, the panel, WiFi), then loops on one event queue. Callbacks from
// other tasks and timers (WiFi, OTA, keys, BLE, SNTP, the 1 s tick) only post
// an app_event_t; every decision and every interface call happens in this
// loop, on the watched application task. Each tick runs the regions below,
// draws the screen, and runs the task (a reading, the due sends, then deep
// sleep) once the window allows it.
//
// File map, one "──" section per Device Logic region:
//   events and callbacks · BLE setup (§26) · sleep (§21) · weather (§23) ·
//   add-on values (§29.2) · sampling setups (§28) · summary email (§25) ·
//   cellular uplink (§33) · push alerts (§31) · display (§6) ·
//   setup check (§24) · connectivity (§5.3) · app_main.
//
// TDS3_TEST_* macros select the test builds under tests/target/; the shipped
// build defines none of them.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ble_settings_core.h"
#include "ble_setup.h"
#include "board.h"
#include "button.h"
#include "deep_sleep.h"
#include "device_logic.h"
#include "esp_app_desc.h"
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
#include "history.h"
#include "history_core.h"
#include "identity.h"
#include "lcd.h"
#include "loop.h"
#include "mdns_name.h"
#if !BOARD_HAS_NOTECARD
#include "max17048.h"
#include "modem_link.h"
#endif
#include "notify.h"
#include "ota_rx.h"
#include "portal.h"
#include "rtc_clock.h"
#include "sampling.h"
#if BOARD_HAS_SD_CARD
#include "settings_card.h"
#endif
#include "sampling_core.h"
#include "sdkconfig.h"
#include "setup_check.h"
#include "status_http.h"
#ifdef TDS3_TEST_PIN_HOLD
#include "driver/gpio.h"
#include "tdisplay_s3_amoled.h"
#endif
#include "summary.h"
#include "time_sync.h"
#include "udp_log.h"
#include "uplink.h"
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
#define MAIL_WIFI_MS 15000                    // FR-25.14: no WiFi within 15 s drops the summary
#ifdef TDS3_TEST_HOLD_S
#define HOLD_TIMEOUT_S TDS3_TEST_HOLD_S  // the sleep test builds cut the hold (R-22)
#else
#define HOLD_TIMEOUT_S DL_HOLD_TIMEOUT_S  // FR-24.6
#endif

typedef enum { APP_TICK, APP_CONN, APP_TIME_SYNCED, APP_KEY, APP_BLE, APP_TAP, APP_PHONE_TIME } app_kind_t;

typedef struct {
    app_kind_t kind;
    dl_conn_event_t ev;
    dl_ble_event_t bev;  // APP_BLE
    bool guard;        // APP_TAP: a double press (FR-28.4)
    pc_creds_t creds;  // DL_EV_CREDS_SUBMITTED
    char ip[16];       // DL_EV_IP
    int64_t ms, at_us;  // APP_PHONE_TIME: the phone's clock and when it came (FR-39.5)
} app_event_t;

static QueueHandle_t queue;
static SemaphoreHandle_t got_ip;  // a summary on a timer wake waits for it (FR-25.6)
static bool net_ready;            // wifi_net_init done
static esp_timer_handle_t tick_timer, retry_timer, attempt_timer, confirm_timer, led_timer;
static const esp_partition_t *downloaded;  // the slot a successful OTA wrote (T11)

static dl_view_t view;
static char softap_ssid[ID_NAME_MAX], hostname[ID_NAME_MAX], ip[16];
static pc_creds_t stored;
_Static_assert(sizeof stored.ssid == WIFI_NET_SSID_MAX + 1 &&
               sizeof stored.password == WIFI_NET_PASSWORD_MAX + 1,
               "portal and wifi_net credentials differ in size");

// FR-23.7, FR-27.4, FR-28.3: the reading section, the history link when one is
// kept, and the setup with the tests still to run.
static void page_extra(char *out, size_t len)
{
    weather_html(out, len);
    size_t n = strlen(out);
    if (history_available() && n < len) {
        snprintf(out + n, len - n, "<p><a href=\"/api/history\">Download history (CSV)</a></p>");
        n = strlen(out);
    }
    const sp_sel_t *sel = sampling_sel();
    sm_collect_t c = sp_collect(sel);
    if (n < len) {
        snprintf(out + n, len - n, "<p>Setup: %s, a reading every %u min, %u min averages",
                 sp_normal_id((sp_normal_t)sel->normal), c.sample_min, c.average_min);
        n = strlen(out);
    }
    for (int t = 0, first = 1; t < SP_TESTS && n < len; t++) {
        if (sel->tests & SP_BIT(t)) {
            snprintf(out + n, len - n, "%s%s", first ? "; tests to run: " : ", ",
                     sp_test_id((sp_test_t)t));
            first = 0;
            n = strlen(out);
        }
    }
    if (n < len) {
        snprintf(out + n, len - n, "</p>");
    }
}

// FR-25.1, FR-28.1, FR-29.11, FR-31.1, FR-33.1: the portal form's mail section,
// its setup section, its loop section, its alerts section, then its uplink section.
static char form_html[8192];

// The portal's extra fields: each component parses its own part of the body
// into a staged copy; the first error stops the submission and is shown to the
// user. The staged settings are stored only once the whole form is accepted.
static const char *form_take(const char *body, size_t len)
{
    const char *bad = summary_take(body, len);
    bad = bad ? bad : sampling_take(body, len);
    bad = bad ? bad : loop_take(body, len);
    bad = bad ? bad : notify_take(body, len);
    return bad ? bad : uplink_take(body, len);
}

static const portal_extra_t form_extra = {.html = form_html, .take = form_take};

// The form sections, concatenated once on first use.
static const portal_extra_t *form_fields(void)
{
    if (!form_html[0]) {
        snprintf(form_html, sizeof form_html, "%s%s%s%s%s", summary_portal_fields()->html,
                 SAMPLING_FORM_HTML, LOOP_FORM_HTML, notify_form_html(), uplink_form_html());
    }
    return &form_extra;
}

static const status_http_source_t status_source = {.ip = ip, .hostname = hostname,
                                                   .rssi = wifi_net_rssi,
                                                   .log = web_log_http,
                                                   .extra_html = page_extra};

static dl_wake_t wake;  // how this boot began (FSD §21); a test's end counts as a key wake
static sp_run_t *run;           // the run of tests (FSD §28), in RTC memory
static bool test_result;        // line 5 shows the verdict in this window (FR-28.11)
static bool test_mail;          // the result email is owed (FR-28.13)
static bool test_alert;         // the test alert is owed (FR-31.7)
static uint32_t quick_next_s;   // uptime of quick's next reading (FR-28.6)
static uint32_t key_used_s;     // a press that ended the hold or a test is no half of a double
static bool task_ran;
static char reading_line[DL_LINE_MAX];  // screen line 4 (FR-23.12)
static char prompt_line[DL_LINE_MAX];   // screen line 5 (FR-24.13)
static char addons_line[DL_LINE_MAX];   // screen line 6 (FR-29.8)

static dl_hold_t hold;         // the setup check (FSD §24)
static uint32_t hold_start_s;  // uptime when the hold began
static int64_t hold_start_us;  // the same, for the LED's half periods (FR-24.9)
static uint32_t live_read_s;   // uptime of the last live reading (FR-24.3)
static uint32_t window_start_s;  // uptime the window counts from (FR-24.11)

static dl_ble_ctx_t ble;               // the BLE setup windows (FSD §26)
static volatile int32_t ble_passkey = -1;  // shown on line 5 while pairing (FR-26.23)
static dl_disp_t disp = DL_LCD_START;

static char shown[DL_LINES][DL_LINE_MAX];  // what the panel shows now
#if !BOARD_HAS_NOTECARD
static RTC_DATA_ATTR dl_show_t show_on;  // FR-35.2: the display chosen, kept for the next window
static uint32_t show_from_s;             // the uptime its window started at (FR-35.24)
#endif
static bool redrawn[DL_LINES];            // lines drawn on this tick, for L7

// ── Events and callbacks ─────────────────────────────────────────────────
//
// The callbacks below run in other tasks (WiFi, httpd, esp_timer, NimBLE) and
// only post an event; the main loop in app_main() handles it. The queue never
// blocks a sender: an event that finds it full is dropped.

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

// True while the running image came from an OTA and is not yet confirmed: it
// must reach ONLINE before CONFIRM_DEADLINE_US or it is rolled back (T5, T13).
static bool image_pending_verify(void)
{
    esp_ota_img_states_t st;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

// From wifi_net: an address (T5, or T9 when already ONLINE) or a lost link.
// `got_ip` also wakes a timer-wake send that waits for WiFi without the queue.
static void on_net(wifi_net_event_t ev, const char *addr)
{
    if (ev == WIFI_NET_EV_GOT_IP && got_ip) {
        xSemaphoreGive(got_ip);
    }
    if (!queue) {
        return;  // a timer wake sending a summary: no connectivity region yet
    }
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

// From ota_rx when a download ends: log it, remember the slot it wrote, and
// let the connectivity region decide (T11 restarts into it, T12 stays).
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

// FR-26.17: no download while a BLE key window is open.
static const char *ota_hold(void)
{
    return dl_ble_holds_task(&ble) ? "ble setup active" : NULL;
}

static const ota_rx_hooks_t ota_hooks = {.on_result = on_ota,
                                         .on_refused = web_log_ota_refused,
                                         .note = status_http_note,
                                         .hold = ota_hold};

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

static void on_tap(bool twice)
{
    app_event_t e = {.kind = APP_TAP, .guard = twice};
    post(&e);
}

static void on_submit(const pc_creds_t *c)
{
    // T3, or T16 with WiFi off (FR-34.5); uplink_take read the switch just before.
    app_event_t e = {.kind = APP_CONN, .guard = true, .creds = *c,
                     .ev = uplink_taken_wifi_on() ? DL_EV_CREDS_SUBMITTED
                                                  : DL_EV_SUBMITTED_WIFI_OFF};
    post(&e);
}

// ── BLE setup region (FSD §26) ───────────────────────────────────────────

static void post_ble(dl_ble_event_t ev)
{
    app_event_t e = {.kind = APP_BLE, .bev = ev};
    post(&e);
}

static void on_key_held(void)
{
    post_ble(DL_BEV_KEY_HELD);  // FR-26.2
}

// §27.3: "history=<header>" tells the page this device keeps a history.
static size_t ble_read_info(char *out, size_t cap)
{
    size_t n = bset_info(esp_app_get_description()->version, hostname,
                         dl_conn_name(view.conn), out, cap);
    if (history_available() && n < cap) {
        char header[HIST_HEADER_MAX];
        size_t h = hist_header(weather_ext(), header);  // §29.2: with the add-on columns
        header[h ? h - 1 : 0] = '\0';                    // without its newline
        int k = snprintf(out + n, cap - n, "&history=%s", header);
        n += k > 0 && (size_t)k < cap - n ? (size_t)k : 0;
    }
    return n;
}

static size_t ble_read_settings(char *out, size_t cap)
{
    static nf_settings_t alerts;  // the NimBLE host task only
    alerts = *notify_settings();
    strlcpy(alerts.topic, notify_topic(), sizeof alerts.topic);  // FR-31.3, as the portal shows it
    size_t n = bset_dump(&stored, summary_settings(), sampling_sel(), loop_settings(), &alerts,
                         uplink_settings(), BOARD_HAS_NOTECARD ? NULL : uplink_modem(),
                         uplink_wifi_on(), out,
                         cap);  // FR-26.9, FR-26.21, FR-28.3, FR-29.11, FR-31.2, FR-33.2, FR-34.3
    memset(alerts.token, 0, sizeof alerts.token);
    return n;
}

// FR-26.6, FR-26.7: a settings body applied as in the provisioning window or a
// key window; the application task acts on the event it posts. From the NimBLE
// host task (BLE) or the application task (the card, FR-35.4), never both at once.
static void settings_apply(const char *body, size_t len, bool provisioning, char *result,
                           size_t cap)
{
    static char merged[CONFIG_BLE_SETUP_BODY_MAX + BSET_MERGE_EXTRA];
    size_t n = 0;
    const char *bad = bset_prepare(body, len, provisioning ? BSET_PROVISION : BSET_CHANGE,
                                   &stored, summary_settings()->password, merged,
                                   sizeof merged, &n);
    app_event_t e = {.kind = APP_CONN, .guard = true,
                     .ev = provisioning ? DL_EV_CREDS_SUBMITTED : DL_EV_BLE_SETTINGS};
    if (!bad) {
        bad = form_take(merged, n);  // FR-25.16, FR-28.1
    }
    bool wifi_on = true;
    if (!bad && pc_parse_wifi(merged, n, &e.creds, &wifi_on) != PC_OK) {
        bad = "ssid";  // bset_prepare already passed the same check
    }
    if (provisioning && !wifi_on) {
        e.ev = DL_EV_SUBMITTED_WIFI_OFF;  // T16 (FR-34.5)
    }
    memset(merged, 0, sizeof merged);  // held the passwords
    if (bad) {
        snprintf(result, cap, "invalid=%s", bad);
    } else {
        snprintf(result, cap, "ok");
        post(&e);
    }
}

static void ble_apply(const char *body, size_t len, char *result, size_t cap)
{
    settings_apply(body, len, ble.state == DL_BLE_PROV, result, cap);
    web_log_ble_settings(result);  // L29
}

#if BOARD_HAS_SD_CARD
// FR-35.4: the card's file as a BLE write in the state the device is in.
static void card_apply(const char *body, size_t len, char *result, size_t cap)
{
    settings_apply(body, len, view.conn == DL_PROVISIONING, result, cap);
}
#endif

static void ble_passkey_cb(uint32_t passkey, bool shown_now)
{
    ble_passkey = shown_now ? (int32_t)passkey : -1;
}

static void ble_event_cb(ble_setup_event_t ev)
{
    switch (ev) {
    case BLE_SETUP_EV_CONNECTED:
        history_ble_reset();  // §27.3: every line again
        post_ble(DL_BEV_CONNECTED);
        break;
    case BLE_SETUP_EV_DISCONNECTED: post_ble(DL_BEV_DISCONNECTED); break;
    case BLE_SETUP_EV_PAIRED: post_ble(DL_BEV_PAIRED); break;
    case BLE_SETUP_EV_PAIR_FAILED: post_ble(DL_BEV_PAIR_FAILED); break;
    case BLE_SETUP_EV_DONE: post_ble(DL_BEV_DONE); break;
    case BLE_SETUP_EV_FORGOTTEN: break;  // FR-26.13: logged by ble_setup
    }
}

// FR-39.5: the phone's clock, from the NimBLE host task to the application task.
static void ble_on_time(int64_t epoch_ms)
{
    app_event_t e = {.kind = APP_PHONE_TIME, .ms = epoch_ms, .at_us = esp_timer_get_time()};
    post(&e);
}

static const ble_setup_config_t ble_cfg = {.name = softap_ssid,
                                           .read_info = ble_read_info,
                                           .read_settings = ble_read_settings,
                                           .read_form = BOARD_HAS_NOTECARD ? bset_form
                                                                           : bset_form_modem,  // FR-26.19
                                           .on_apply = ble_apply,
                                           .on_passkey = ble_passkey_cb,
                                           .on_event = ble_event_cb,
                                           .read_history = history_ble_read,       // §27.3
                                           .on_history_range = history_ble_range,
                                           .on_time = ble_on_time};

static uint32_t uptime_s(void);

static void ble_event(dl_ble_event_t ev)
{
    // FR-26.4, FR-26.17: never on a timer wake, with a failed panel, or during a download.
    bool allowed = wake != DL_WAKE_TIMER && disp != DL_LCD_FAILED && !ota_rx_busy();
    dl_ble_step_t step = dl_ble_step(&ble, ev, uptime_s(), allowed,
                                     view.conn == DL_PROVISIONING);
    unsigned a = step.actions;
    if (a & DL_B_STOP) {
        esp_err_t err = ble_setup_stop();  // FR-26.3, FR-26.18
        ble_passkey = -1;
        if (err != ESP_OK) {
            web_log_warn("ble stop", err);
        }
    }
    if (a & DL_B_LOG_OFF) {
        web_log_ble_off(step.reason);  // L30
    }
    if (a & DL_B_START) {
        esp_err_t err = ble_setup_start(&ble_cfg);
        if (err != ESP_OK) {
            web_log_warn("ble start", err);
        }
    }
    if (a & DL_B_LOG_ON) {
        web_log_ble_on(step.reason);  // L27
    }
    if (a & (DL_B_LOG_PAIRED | DL_B_LOG_PAIR_FAIL)) {
        web_log_ble_pairing(a & DL_B_LOG_PAIRED);  // L28
    }
    if (a & DL_B_START_WINDOW) {
        window_start_s = uptime_s();  // FR-26.5
    }
}

static esp_err_t ble_stop_cb(void *arg)
{
    return ble_setup_stop();  // a backstop: no window is open when the task runs
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

// FR-27.1: the reading into the history; L31 for a missing partition once per
// boot (FR-27.8), and for each failed append (FR-27.9). True when appended.
// The text of the last sensor and history failure, an alert's body (§31.3),
// kept across sleep for an alert sent on a later run.
static RTC_DATA_ATTR char sensor_fail[48], history_fail[48];
// The newest L34 or L35 this boot, a loop alert's body (§31.3); not in RTC
// memory, which is full: an alert retried on a later boot says "no reading".
static char loop_line[LP_LOG_MAX];

static bool history_add(const wx_reading_t *r)
{
    static bool told;
    esp_err_t err = history_append(r);
    if (err == ESP_ERR_NOT_FOUND) {
        if (!told) {
            web_log_history_unavailable();
            told = true;
        }
        wl_fmt_history_unavailable(history_fail, sizeof history_fail);
    } else if (err != ESP_OK) {
        web_log_history_failed(err);
        wl_fmt_history_failed(history_fail, sizeof history_fail, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

// FR-31.4, FR-31.14: a reading (NULL: the sensor failed) against the alert
// limits and failures; nothing is decided while alerts are off.
static void observe_alerts(const wx_reading_t *r, bool appended)
{
    if (!notify_enabled()) {
        return;
    }
    nf_state_t *st = notify_state();
    nf_observe(st, NF_SENSOR, r == NULL);
    if (r) {
        int32_t v[NF_QTY];
        nf_values(r, v);
        nf_observe_values(st, notify_settings(), v);
        nf_observe(st, NF_HISTORY, !appended);
        if (loop_settings()->on) {
            nf_observe(st, NF_LOOP, !wx_ext_has(r->ext[WX_EXT_LOOP]));  // §31.3 loop
        }
    }
}

// ── Add-on values (FSD §29.2) ────────────────────────────────────────────

// FR-29.2 – FR-29.5: the loop's value for a reading just sampled, logged as
// L34, or L35 for a fault or an ADC that did not answer; never retried.
static void read_addons(wx_reading_t *r)
{
    const lp_settings_t *lp = loop_settings();
    if (!lp->on) {
        return;  // FR-29.1
    }
    float ma, value;
    bool ok;
    char line[LP_LOG_MAX];
    esp_err_t err = loop_read(&ma, &value, &ok);
    if (err == ESP_OK && ok) {
        r->ext[WX_EXT_LOOP] = value;
        lp_log_value(ma, value, lp->unit, line);
    } else {
        lp_log_failed(err == ESP_OK ? NULL : esp_err_to_name(err), ma, line);
    }
    web_log_loop(line, err == ESP_OK && ok);
    snprintf(loop_line, sizeof loop_line, "%s", line);
}

// §29.2: which add-ons are on, from their stored settings; set once a boot,
// before the first reading (a settings change restarts the device).
static void addons_setup(void)
{
    esp_err_t err = loop_init();  // NFR-29.2: the supply low, the loop on or off
    if (err != ESP_OK) {
        web_log_warn("loop supply", err);
    }
    wx_ext_cfg_t cfg = {0};
    lp_ext_def(loop_settings(), &cfg.s[WX_EXT_LOOP]);
    weather_set_ext(&cfg, read_addons);
}

// §29.2: the add-on slots that are on, as sp_reading_ext's mask.
static uint8_t addons_on(void)
{
    uint8_t mask = 0;
    for (int i = 0; i < WX_EXT; i++) {
        mask |= weather_ext()->s[i].on ? (uint8_t)(1u << i) : 0;
    }
    return mask;
}

static void show_newest(void)
{
    wx_screen_line(weather_newest(), reading_line);       // FR-23.12
    wx_ext_line(weather_newest(), weather_ext(), addons_line);  // FR-29.8
}

// One reading, logged (L21) and kept, or the failure logged (L22); either way the
// boot carries on (FR-23.4). Line 4 shows the newest kept reading. True when the
// reading was taken and appended to the history (FR-28.10).
static bool take_reading(void)
{
    wx_reading_t r;
    bool ok = false;
    weather_set_average(run && sp_running(run, SP_CYCLE)
                            ? SP_CYCLE_AVG_MIN
                            : sp_collect(sampling_sel()).average_min);  // FR-23.15, FR-28.6
    esp_err_t err = weather_read(&r);
    if (err == ESP_OK) {
        char iso[WX_ISO_MAX], line[WL_LINE_MAX];
        weather_iso(&r, iso);
        wx_log_line(&r, iso, line, sizeof line);
        web_log_reading(line);
        ok = history_add(&r);
        observe_alerts(&r, ok);
    } else {
        web_log_bme280_failed(err);
        wl_fmt_bme280_failed(sensor_fail, sizeof sensor_fail, esp_err_to_name(err));
        observe_alerts(NULL, false);
    }
    show_newest();
    return ok;
}

// FR-24.3: during the hold, line 4 shows a live reading, neither kept nor logged.
static void show_live_reading(void)
{
    wx_reading_t r;
    wx_screen_line(weather_sample(&r) == ESP_OK ? &r : NULL, reading_line);
}

// ── Sampling setups region (FSD §28) ─────────────────────────────────────


// FR-28.6, FR-28.10: one reading of the running test; the seconds until its next
// reading, 0 after its last.
static uint32_t test_reading(void)
{
    bool ok = take_reading();
    const wx_reading_t *r = weather_newest();
    const wx_avgs_t *a = weather_averages();
    uint8_t want = addons_on(), have = 0;  // FR-29.10, FR-29.14
    for (int i = 0; ok && r && i < WX_EXT; i++) {
        have |= wx_ext_has(r->ext[i]) ? (uint8_t)(1u << i) : 0;
    }
    return sp_reading_ext(run, ok, want, have, ok && r && r->time >= WX_TIME_SET_MIN,
                          a->cur_start, a->closed);
}

// FR-28.9, FR-28.12: the test starts (L32); stored as running, so a reset
// during it is seen on the next boot (FR-28.8).
static void test_start(sp_test_t t)
{
    sp_start(run, t);
    esp_err_t err = sampling_set(sampling_sel()->tests & (uint8_t)~SP_BIT(t), (int)t);
    if (err != ESP_OK) {
        web_log_warn("test store", err);
    }
    char line[SP_LOG_MAX];
    sp_log_start(t, line);
    web_log_test(line, true);
}

// FR-28.8, FR-28.12: the running test ends (L33); ended early, the remaining
// tests are dropped.
static void test_end(sp_by_t by)
{
    sp_result_t res = sp_end(run, by);
    char line[SP_LOG_MAX];
    sp_log_end(&res, weather_ext(), line);
    web_log_test(line, sp_pass(&res));
    esp_err_t err = sampling_set(by == SP_BY_NONE ? sampling_sel()->tests : 0, -1);
    if (err != ESP_OK) {
        web_log_warn("test store", err);
    }
}

// FR-28.5: the next stored test starts; false when none is left.
static bool test_next(void)
{
    int t = sp_next(sampling_sel()->tests);
    if (t < 0) {
        return false;
    }
    test_start((sp_test_t)t);
    return true;
}

// FR-28.11, FR-28.13: the run is over: the verdict on line 5 and the 60 s
// window from now, the result email owed, then the task with the normal setup.
static void tests_over(void)
{
    test_result = true;
    test_mail = sm_enabled(summary_settings());
    test_alert = notify_enabled();  // FR-31.7
    task_ran = false;
    window_start_s = uptime_s();
}

// FR-28.6: quick's next reading when due; after its last, the next test or the end.
static void run_task_and_sleep(void);
static void send_alerts_awake(void);
static void send_summary(bool net, const char *nolink);
#if BOARD_HAS_NOTECARD
static void send_summary_notes(void);
#endif
static bool mail_note(int i, const char *subject, const char *text, uint32_t n);
static void gps_test_begin(void);
static void gps_test_end(sp_by_t by);

static void quick_tick(void)
{
    if (!sp_running(run, SP_QUICK) || uptime_s() < quick_next_s) {
        return;
    }
    uint32_t s = test_reading();
    send_alerts_awake();  // FR-31.10
    if (s) {
        quick_next_s = uptime_s() + s;
        return;
    }
    test_end(SP_BY_NONE);
    if (!test_next()) {
        tests_over();
    } else if (sp_running(run, SP_GPS)) {
        gps_test_begin();  // FR-33.20: awake, as quick
    } else {
        run_task_and_sleep();  // cycle: its first reading, then asleep
    }
}

// FR-28.4, FR-28.8: a short press of the user key ends quick; a double press
// starts it when nothing else holds the device awake.
static void tap(bool twice)
{
    uint32_t now = uptime_s();
    if (sp_running(run, SP_GPS)) {  // FR-33.20: as quick
        gps_test_end(SP_BY_KEY);
        key_used_s = now;
        tests_over();
        return;
    }
    if (sp_running(run, SP_QUICK)) {
        test_end(SP_BY_KEY);
        key_used_s = now;
        tests_over();
        return;
    }
    bool provisioned = view.conn != DL_BOOT && view.conn != DL_PROVISIONING;
    if (!twice || run->running || hold == DL_HOLD_ON || !provisioned ||
        dl_ble_holds_task(&ble) || now - key_used_s < 2) {
        return;
    }
    sp_begin(run);
    test_result = false;
    test_start(SP_QUICK);
    quick_next_s = now + test_reading();
    send_alerts_awake();  // FR-31.10
}

// FR-28.13: the result email, once ONLINE in the window after the tests, or
// dropped (L26 wifi) when the window ends first.
// FR-34.10, FR-34.11: the path of an alert and of an email; with WiFi off,
// what would go over WiFi is not sent at all.
static ul_via_t alert_via(void)
{
    return ul_gate(ul_alert_via(uplink_settings()), uplink_wifi_on());
}

static ul_via_t mail_via(void)
{
    return ul_gate(ul_mail_via(uplink_settings()), uplink_wifi_on());
}

static void send_test_mail(bool online)
{
    test_mail = false;
    static char body[SP_LOG_MAX * SP_TESTS + 8];
    char subject[SM_SUBJECT_MAX];
    sp_mail_subject(run, hostname, subject);
    sp_mail_body(run, weather_ext(), body, sizeof body);
    uint32_t n, m;
    sp_verdict(run, &n, &m);
    ul_via_t via = mail_via();
    if (via != UL_VIA_WIFI) {  // FR-33.6, FR-33.13: a note each, or no email at all
        for (int i = 0; via == UL_VIA_CELL && summary_to(i); i++) {
            mail_note(i, subject, body, n);
        }
        return;
    }
    for (int i = 0; summary_to(i); i++) {
        mt_reason_t why = online ? summary_send_text(hostname, i, subject, body) : MT_OK;
        if (!online || why != MT_OK) {
            web_log_mail_failed(summary_to(i), online ? mt_reason_name(why) : "wifi");  // L26
        } else {
            web_log_mail_sent(summary_to(i), n);  // L25
        }
    }
}

// ── Summary email region (FSD §25) ───────────────────────────────────────

// FR-25.6: on a timer wake the station comes up only for the summary.
static bool mail_wifi_up(void)
{
    got_ip = xSemaphoreCreateBinary();
    if (!got_ip) {
        return false;
    }
    if (!net_ready) {
        if (wifi_net_init(on_net, hostname) != ESP_OK) {
            return false;
        }
        net_ready = true;
    }
    return wifi_net_creds_load(stored.ssid, stored.password) &&
           wifi_net_sta_start(stored.ssid, stored.password) == ESP_OK &&
           xSemaphoreTake(got_ip, pdMS_TO_TICKS(MAIL_WIFI_MS)) == pdTRUE;
}

// ── The cellular build's modem (FSD §35) ─────────────────────────────────

#if !BOARD_HAS_NOTECARD
static int modem_users;  // sends sharing the link that is up
static bool modem_ok;    // the link came up for them
static bool modem_sntp;  // SNTP started for the link's time (FR-35.13)
static bool unwatched;   // the watch paused for a modem call

// FR-19.1, FR-35.21: a modem call (a wake, an attach, a GNSS start) can wait
// longer than the watch's 10 s; its own limits bound it, so the task is not
// watched for its length.
static void watch_pause(void)
{
    unwatched = esp_task_wdt_status(NULL) == ESP_OK && esp_task_wdt_delete(NULL) == ESP_OK;
}

static void watch_resume(void)
{
    if (unwatched) {
        esp_task_wdt_add(NULL);
        unwatched = false;
    }
}

static void rtc_after_sync(int res_ms);

// FR-35.13: on a boot whose clock was never set, the network's time, else
// SNTP over the link; L20 src=cell when the clock is set. FR-39.7: with an
// RTC, the network's time is taken at every attach that supplies one.
static void modem_time(const modem_link_t *r)
{
    ul_mtime_t src = ul_modem_time(time_sync_synced(), r->net_time != 0);
    if (src == UL_MT_NONE && r->net_time != 0 && rtc_clock_present()) {
        src = UL_MT_NET;
    }
    if (src == UL_MT_NET && time_sync_set(r->net_time) == ESP_OK) {
        web_log_time_synced("cell");  // L20
        rtc_after_sync(RC_RES_CELL_MS);  // FR-39.4, FR-39.8
        return;
    }
    if (src != UL_MT_NONE && time_sync_start(NULL) == ESP_OK) {
        modem_sntp = true;
        for (int ms = 0; ms < UL_SNTP_MS && !time_sync_synced(); ms += 100) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (time_sync_synced()) {
            web_log_time_synced("cell");  // L20
        }
    }
}

// FR-35.8, FR-35.9: the link for the sends that follow, until the matching
// modem_end; L46 after a full attach, L47 when it does not come up.
static bool modem_begin(void)
{
    if (modem_users++ > 0) {
        return modem_ok;
    }
    watch_pause();
    modem_link_t r;
    modem_ok = modem_link_up(uplink_modem(), !time_sync_synced(), &r);
    if (r.attached) {
        web_log_modem_attach(r.attach_ms, r.net, r.csq);  // L46
    }
    if (!modem_ok) {
        web_log_modem_failed(r.failed, r.failed_ms);  // L47
    } else {
        modem_time(&r);
    }
    watch_resume();
    return modem_ok;
}

// FR-35.23: after the last send, the modem powered down and its supply cut.
static void modem_end(void)
{
    if (--modem_users == 0 && modem_ok) {
        if (modem_sntp) {
            time_sync_stop();
            modem_sntp = false;
        }
        modem_link_down();
    }
}
#endif

// ── Hardware clock region (FSD §39) ──────────────────────────────────────

// FR-39.2, FR-39.4, FR-39.9: the RTC read at boot, the clock set from it.
static void rtc_boot(bool timer_wake)
{
    esp_err_t err;
    rc_boot_t b = rtc_clock_boot(timer_wake, &err);
    if (b.log_synced) {
        web_log_time_synced("rtc");  // L20
    }
    if (b.log_lost) {
        web_log_rtc(ESP_OK);  // L56 lost
    }
    if (b.log_failed) {
        web_log_rtc(err);  // L56 failed
    }
}

// FR-39.4, FR-39.8: after a network sync, the RTC measured, trimmed and set.
static void rtc_after_sync(int res_ms)
{
    rc_drift_t d;
    esp_err_t err;
    if (!rtc_clock_net_sync(res_ms, &d, &err)) {
        return;  // FR-39.1: none fitted
    }
    if (d.log) {
        web_log_rtc_drift(d.err_ms, d.days_x10, d.trim, d.trimmed ? d.was : -100);  // L55
    }
    if (err != ESP_OK) {
        web_log_rtc(err);  // L56 failed
    }
}

// FR-39.5: the phone's clock, taken when the clock is unsynced or 2 s off.
static void rtc_phone(const app_event_t *e)
{
    esp_err_t err;
    if (rtc_clock_phone(e->ms, e->at_us, &err)) {
        web_log_time_synced("phone");  // L20
    }
    if (err != ESP_OK) {
        web_log_rtc(err);  // L56 failed
    }
}

// ── Cellular uplink region (FSD §33) ─────────────────────────────────────

#if BOARD_HAS_NOTECARD
static ul_time_t cell_time;  // FR-33.12: the clock from the Notecard
#endif
static ul_gps_t gps;         // FR-33.15 – FR-33.18: the search in this hold

#if BOARD_HAS_NOTECARD
static void cell_failed(const char *why, const char *req)
{
    if (why) {
        web_log_cell_failed(why, req);  // L44
    }
}
#endif

// FR-33.3, FR-33.4, FR-33.14: the Notecard set up once per boot, only with
// cell; L44 once when it does not answer, and that boot's notes then fail.
static bool cell_up(void)
{
    static bool logged;
    if (!BOARD_HAS_NOTECARD || !uplink_settings()->cell) {
        return false;  // §35.3: no Notecard on the cellular build
    }
    const char *req = NULL;
    const char *why = uplink_start(&req);
    if (why && !logged) {
        logged = true;
        web_log_cell_failed(why, req);
    }
    return why == NULL;
}

#if BOARD_HAS_NOTECARD
// FR-33.12: one card.time; the clock set and L20 src=cell when it has a time.
static void cell_time_ask(bool window)
{
    int64_t t;
    unsigned a;
    if (uplink_time(&t) && time_sync_set(t) == ESP_OK) {
        web_log_time_synced("cell");  // L20
        rtc_after_sync(RC_RES_CELL_MS);  // FR-39.4
        a = ul_time_step(&cell_time, UL_TE_TIME, window, uptime_s());
    } else {
        a = ul_time_step(&cell_time, UL_TE_NO_TIME, window, uptime_s());
    }
    if (a & UL_TA_SYNC) {
        cell_failed(uplink_hub_sync(), "hub.sync");
    }
}

// FR-33.4, FR-33.12: at boot with cell, the Notecard set up, then the clock
// asked for when it was never set (in a window again every 5 s, FR-33.12).
static void cell_boot(bool window)
{
    if (!cell_up()) {
        return;
    }
    if (!time_sync_synced()) {
        cell_time_ask(window);
        return;
    }
    // FR-39.6: with the clock from the RTC (FR-39.3: no wait), a window takes
    // the Notecard's time once when its last sync is recent enough.
    if (!window || !rtc_clock_present()) {
        return;
    }
    double age = -1;
    bool have = uplink_sync_age(&age);
    int64_t t;
    if (rc_cell_fresh(have, age) && uplink_time(&t) && time_sync_set(t) == ESP_OK) {
        web_log_time_synced("cell");  // L20
        rtc_after_sync(RC_RES_CELL_MS);
    }
}
#else
// FR-35.13: in a window whose clock was never set, the modem link is brought
// up for the time alone; on a timer wake only a due send brings it up.
static void cell_boot(bool window)
{
    if (window && ul_modem_wanted(uplink_settings(), false, false, false, !time_sync_synced(),
                                  false)) {
        modem_begin();
        modem_end();
    }
}
#endif

#if BOARD_HAS_NOTECARD
// FR-33.6, FR-33.8 – FR-33.11: a note; L43 when the Notecard takes it, else L44.
static bool cell_note(const char *file, const char *what, const char *body)
{
    const char *why = !cell_up() ? "timeout" : body ? uplink_note(file, body, true) : "err";
    if (why) {
        web_log_cell_failed(why, "note.add");  // L44
        return false;
    }
    web_log_cell_note(what, 1);  // L43
    return true;
}
#endif

#if !BOARD_HAS_NOTECARD
// FR-35.8: on the cellular build an alert with `uplink` `cell` goes by §31's
// sender over the modem, with its logs; L40 connect when the link is down.
static bool alert_note(const char *title, const char *body, bool good, const char *event)
{
    bool up = modem_begin();
    if (up) {
        notify_begin();
    }
    nt_reason_t why = up ? notify_send(hostname, title, body, good) : NT_ERR_CONNECT;
    modem_end();
    if (why != NT_OK) {
        web_log_ntfy_failed(nt_reason_name(why), event);  // L40
        return false;
    }
    web_log_ntfy_sent(event);  // L39
    return true;
}

// FR-35.8: likewise one email (a test result, FR-28.13) by §25's sender.
static bool mail_note(int i, const char *subject, const char *text, uint32_t n)
{
    bool up = modem_begin();
    mt_reason_t why = up ? summary_send_text(hostname, i, subject, text) : MT_ERR_CONNECT;
    modem_end();
    if (why != MT_OK) {
        web_log_mail_failed(summary_to(i), mt_reason_name(why));  // L26
        return false;
    }
    web_log_mail_sent(summary_to(i), n);  // L25
    return true;
}
#else
// §33.4, FR-33.8: one alert as an alert.qo note, L39 when the Notecard takes it.
static bool alert_note(const char *title, const char *body, bool good, const char *event)
{
    static char note[2048];
    bool fits = ul_alert_body(note, sizeof note, notify_topic(), title, nf_priority(good),
                              nf_tags(good), body) > 0;
    if (!cell_note("alert.qo", "alert", fits ? note : NULL)) {
        return false;
    }
    web_log_ntfy_sent(event);  // L39
    return true;
}

// §33.4, FR-33.8: one email as a mail.qo note to recipient i, L25 when the
// Notecard takes it.
static bool mail_note(int i, const char *subject, const char *text, uint32_t n)
{
    static char note[SM_BODY_MAX + SM_BODY_MAX / 4 + 256];  // the text escaped, with the fields
    bool fits = ul_mail_body(note, sizeof note, summary_to(i), subject, text) > 0;
    if (!cell_note("mail.qo", "mail", fits ? note : NULL)) {
        return false;
    }
    web_log_mail_sent(summary_to(i), n);  // L25
    return true;
}
#endif

#if !BOARD_HAS_NOTECARD
// FR-35.14: the search with the modem's GNSS; the GNSS and BLDO2 are off
// again at a fix and at the search's end.
static void gps_act(unsigned a)
{
    watch_pause();
    if ((a & UL_GA_CONTINUOUS) && !modem_link_gnss_start(uplink_modem())) {
        modem_link_gnss_stop();
        gps.on = false;  // no search: the gps test fails at once (FR-33.20)
    }
    if (a & UL_GA_FIXED_NEW) {  // FR-33.17
        modem_link_gnss_stop();
        esp_err_t err =
            uplink_place_store(gps.lat, gps.lon, time_sync_synced() ? (int64_t)time(NULL) : 0);
        if (err != ESP_OK) {
            web_log_warn("place store", err);
        }
        web_log_gps(&gps.lat, &gps.lon);  // L45
    }
    if (a & (UL_GA_FIXED_KEPT | UL_GA_OFF)) {  // FR-33.18
        modem_link_gnss_stop();
        web_log_gps(NULL, NULL);  // L45 none
    }
    watch_resume();
}

// FR-35.14: one AT+CGNSINF, as the search's answer.
static ul_gps_ev_t gps_answer(double *lat, double *lon)
{
    bool fix = false;
    double la = 0, lo = 0;
    int64_t utc = 0;
    watch_pause();
    bool read = modem_link_gnss_read(&fix, &la, &lo, &utc);
    watch_resume();
    return ul_gps_modem_answer(&gps, read && fix, la, lo, utc, lat, lon);
}
#else
static void gps_act(unsigned a)
{
    if (a & UL_GA_CONTINUOUS) {
        cell_failed(uplink_location_mode("continuous", NULL, NULL), "card.location.mode");
    }
    if (a & UL_GA_FIXED_NEW) {  // FR-33.17
        esp_err_t err =
            uplink_place_store(gps.lat, gps.lon, time_sync_synced() ? (int64_t)time(NULL) : 0);
        if (err != ESP_OK) {
            web_log_warn("place store", err);
        }
        cell_failed(uplink_location_mode("fixed", &gps.lat, &gps.lon), "card.location.mode");
        web_log_gps(&gps.lat, &gps.lon);  // L45
    }
    if (a & UL_GA_FIXED_KEPT) {  // FR-33.18
        double lat = 0, lon = 0;
        int64_t at = 0;
        if (uplink_place(&lat, &lon, &at)) {
            cell_failed(uplink_location_mode("fixed", &lat, &lon), "card.location.mode");
        }
        web_log_gps(NULL, NULL);  // L45 none
    }
    if (a & UL_GA_OFF) {  // FR-33.18
        cell_failed(uplink_location_mode("off", NULL, NULL), "card.location.mode");
        web_log_gps(NULL, NULL);  // L45 none
    }
}

static ul_gps_ev_t gps_answer(double *lat, double *lon)
{
    return ul_gps_answer(&gps, uplink_location(), lat, lon);
}
#endif

// FR-33.15 – FR-33.18: the GPS search, only in the hold, only with cell and gps on.
static void gps_event(ul_gps_ev_t ev)
{
    // FR-33.16: the Notecard set up first; FR-35.14: the modem starts in gps_act.
    bool wanted = ul_gps_wanted(uplink_settings()) &&
                  (ev != UL_GE_START || !BOARD_HAS_NOTECARD || cell_up());
    double lat = 0, lon = 0;
    int64_t at = 0;
    bool place = ev == UL_GE_END && gps.on && !gps.fixed && uplink_place(&lat, &lon, &at);
    unsigned a = ul_gps_step(&gps, ev, wanted, place, uptime_s(), 0, 0);
    if (a & UL_GA_CONTINUOUS) {  // FR-33.16: only a fix taken after this counts
        gps.after = time_sync_synced() ? (int64_t)time(NULL) : 0;
    }
    gps_act(a);
    if (a & UL_GA_ASK) {
        ul_gps_ev_t got = gps_answer(&lat, &lon);
        gps_act(ul_gps_step(&gps, got, wanted, false, uptime_s(), lat, lon));
    }
}

static uint32_t gps_test_start_s;

// FR-33.20: the gps test starts the search of FR-33.16 outside the hold.
static void gps_test_begin(void)
{
    gps = (ul_gps_t){0};  // nothing from an earlier hold
    gps_test_start_s = uptime_s();
    gps_event(UL_GE_START);
}

// FR-33.20: the gps test ends; the search ends as a hold's does (FR-33.18).
static void gps_test_end(sp_by_t by)
{
    sp_gps_result(run, gps.fixed);
    gps_event(UL_GE_END);
    test_end(by);
    show_newest();  // line 6 back to the add-ons
}

// FR-33.20: the gps test passes at a fix and fails after SP_GPS_S, or at
// once when no search could start (no cell, gps off, no Notecard).
static void gps_test_tick(void)
{
    if (!sp_running(run, SP_GPS)) {
        return;
    }
    if (!gps.on || gps.fixed || uptime_s() - gps_test_start_s >= SP_GPS_S) {
        gps_test_end(SP_BY_NONE);
        tests_over();  // gps runs last
    }
}

// Each tick of a window, before the draw: the clock's asks (FR-33.12), the GPS
// search and its line 6 in the hold or the gps test (FR-33.16, FR-33.19, FR-33.20).
static void cell_tick(void)
{
#if BOARD_HAS_NOTECARD
    if (!uplink_ready()) {
        return;
    }
    if (ul_time_step(&cell_time, UL_TE_TICK, true, uptime_s()) & UL_TA_ASK) {
        cell_time_ask(true);
    }
#endif
    if (hold == DL_HOLD_ON || sp_running(run, SP_GPS)) {
        gps_event(UL_GE_TICK);
        ul_gps_shown(&gps, uptime_s(), addons_line, sizeof addons_line);
    }
}

// ── Push alerts region (FSD §31) ─────────────────────────────────────────

// §31.3: an alert's body, from the newest reading and the last failure texts.
static void alert_body(nf_cond_t c, bool end, char out[NF_BODY_MAX])
{
    const wx_reading_t *r = weather_newest();
    if (c <= NF_P_LOW) {
        if (!r) {
            snprintf(out, NF_BODY_MAX, "no reading");
            return;
        }
        int32_t v[NF_QTY];
        nf_values(r, v);
        char stamp[20];
        bool timed = r->time >= WX_TIME_SET_MIN;  // FR-22.6
        if (timed) {
            time_t t = (time_t)r->time;
            struct tm tm;
            localtime_r(&t, &tm);
            strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &tm);
        }
        nf_limit_body(notify_settings(), c, v, timed ? stamp : NULL, out);
    } else if (c == NF_SENSOR && !end) {
        snprintf(out, NF_BODY_MAX, "%s", sensor_fail);
    } else if (c == NF_SENSOR && r) {
        char iso[WX_ISO_MAX];
        weather_iso(r, iso);
        wx_log_line(r, iso, out, NF_BODY_MAX);  // L21 after "web: "
    } else if (c == NF_LOOP) {
        snprintf(out, NF_BODY_MAX, "%s", loop_line[0] ? loop_line : "no reading");  // L35 / L34
    } else if (c == NF_HISTORY && !end) {
        snprintf(out, NF_BODY_MAX, "%s", history_fail);
    } else {
        snprintf(out, NF_BODY_MAX, "%s", c == NF_HISTORY ? "history appended" : "ok");
    }
}

// FR-31.8, FR-31.11, FR-31.12: every due alert, each its own message, logged
// L39 or L40; a failure stays due for one more try on a later run.
static void send_alerts(bool net)
{
    nf_state_t *st = notify_state();
    notify_begin();
    bool end;
    for (nf_cond_t c = nf_next_due(st, 0, &end); c != NF_CONDS;
         c = nf_next_due(st, (nf_cond_t)(c + 1), &end)) {
        char title[NF_TITLE_MAX], event[NF_EVENT_MAX];
        static char body[NF_BODY_MAX];
        const nf_names_t names = {.loop_name = loop_settings()->name};
        nf_title(hostname, c, end, &names, title);
        nf_event(c, end, event);
        alert_body(c, end, body);
        bool ok;
        ul_via_t via = alert_via();
        if (via == UL_VIA_NONE) {
            nf_result(st, c, true);  // FR-34.11: tracked, not sent
            continue;
        }
        if (via == UL_VIA_CELL) {
            ok = alert_note(title, body, end, event);  // FR-33.6, FR-33.10
        } else {
            nt_reason_t why = net ? notify_send(hostname, title, body, end) : NT_ERR_CONNECT;
            ok = net && why == NT_OK;
            if (ok) {
                web_log_ntfy_sent(event);  // L39
            } else {
                web_log_ntfy_failed(net ? nt_reason_name(why) : "wifi", event);  // L40
            }
        }
        nf_result(st, c, ok);
    }
}

// FR-31.10: awake (a quick test's readings), the alerts go out over the WiFi
// already up.
static void send_alerts_awake(void)
{
    if (notify_enabled() && nf_any_due(notify_state())) {
        send_alerts(view.conn == DL_ONLINE);
    }
}

// FR-31.7: the test alert, once ONLINE in the window after the tests, or
// dropped (L40 wifi) when the window ends first.
static void send_test_alert(bool online)
{
    test_alert = false;
    uint32_t n, m;
    bool pass = sp_verdict(run, &n, &m);
    char title[NF_TITLE_MAX];
    static char body[SP_LOG_MAX * SP_TESTS + 8];
    nf_test_title(hostname, pass, title);
    sp_mail_body(run, weather_ext(), body, sizeof body);
    const char *event = pass ? "test_pass" : "test_fail";
    if (alert_via() == UL_VIA_NONE) {
        return;  // FR-34.11
    }
    if (alert_via() == UL_VIA_CELL) {
        alert_note(title, body, pass, event);  // FR-33.6
        return;
    }
    notify_begin();
    nt_reason_t why = online ? notify_send(hostname, title, body, pass) : NT_ERR_CONNECT;
    if (online && why == NT_OK) {
        web_log_ntfy_sent(event);  // L39
    } else {
        web_log_ntfy_failed(online ? nt_reason_name(why) : "wifi", event);  // L40
    }
}

// FR-25.3 – FR-25.9, FR-31.10: due alerts and a due summary are sent once, or
// dropped and logged; on a timer wake one WiFi start serves both.
// After a reading: sends the due alerts and the due summary, each by its own
// path (WiFi, a Notecard note, or nothing). On a timer wake WiFi is started
// only if one of them goes over WiFi, and stopped again afterwards.
static void send_due_messages(void)
{
    bool mail = summary_due();
    bool alerts = notify_enabled() && nf_any_due(notify_state());
    if (!mail && !alerts) {
        return;
    }
    bool timer = wake == DL_WAKE_TIMER;
    ul_via_t mvia = mail_via();
    // FR-33.6, FR-34.9: with cell or with WiFi off a timer wake never starts WiFi.
    bool wifi = (alerts && alert_via() == UL_VIA_WIFI) || (mail && mvia == UL_VIA_WIFI);
    bool net = wifi && (timer ? mail_wifi_up() : view.conn == DL_ONLINE);
#if !BOARD_HAS_NOTECARD
    // FR-35.7, FR-35.8: one modem link for every send that goes over it.
    bool modem = (alerts && alert_via() == UL_VIA_CELL) || (mail && mvia == UL_VIA_CELL);
    bool cnet = modem && modem_begin();
#endif
    if (alerts) {
        send_alerts(net);
    }
    if (mail && mvia == UL_VIA_CELL) {
#if BOARD_HAS_NOTECARD
        send_summary_notes();
#else
        send_summary(cnet, "connect");  // FR-35.8: §25's sender over the modem
#endif
    } else if (mail && mvia == UL_VIA_NONE) {
        summary_done();  // FR-33.13: the period advances as after a dropped send
    } else if (mail) {
        send_summary(net, "wifi");
    }
#if !BOARD_HAS_NOTECARD
    if (modem) {
        modem_end();
    }
#endif
    if (timer && wifi) {
        wifi_net_stop();  // FR-21.7, FR-25.6, FR-31.10: WiFi only while they are sent
    }
}

#if BOARD_HAS_NOTECARD
// FR-33.6, FR-33.8: the due summary as a mail.qo note to each recipient.
static void send_summary_notes(void)
{
    static char text[SM_BODY_MAX];
    char subject[SM_SUBJECT_MAX];
    uint32_t n = 0;
    summary_compose(hostname, subject, text, sizeof text, &n);
    for (int i = 0; summary_to(i); i++) {  // FR-25.12: each recipient its own email
        if (mail_note(i, subject, text, n)) {
            summary_noted();
        }
    }
    summary_done();
}
#endif

// FR-25.12, FR-25.14: the due summary to each recipient, its own email;
// without a link each is dropped with the reason nolink ("wifi", or
// "connect" for the modem's).
static void send_summary(bool net, const char *nolink)
{
    for (int i = 0; summary_to(i); i++) {  // FR-25.12: each recipient its own email
        uint32_t n = 0;
        mt_reason_t why = net ? summary_send(hostname, i, &n) : MT_OK;
        if (!net || why != MT_OK) {
            web_log_mail_failed(summary_to(i), net ? mt_reason_name(why) : nolink);  // L26
        } else {
            web_log_mail_sent(summary_to(i), n);  // L25
        }
    }
    summary_done();
}

// The task (FR-21.4, FR-21.9): takes a reading (FR-23.1), then returns the sleep
// period it asks for (FR-23.17), 0 to stay awake; or runs the tests (§28). The sleep test build sleeps
// twice per flash with its own period, then stays awake.
static uint32_t run_task(void)
{
    if (test_mail) {
        send_test_mail(false);  // FR-28.13: the window ended before ONLINE
    }
    if (test_alert) {
        send_test_alert(false);  // FR-31.7: the window ended before ONLINE
    }
    test_result = false;
    if (!run->running && sampling_sel()->tests) {  // FR-28.5
        sp_begin(run);
        test_next();
    }
    if (sp_running(run, SP_QUICK)) {  // FR-28.6: awake; the tick takes the rest
        quick_next_s = uptime_s() + test_reading();
        send_alerts_awake();  // FR-31.10
        return 0;
    }
    if (sp_running(run, SP_GPS)) {  // FR-33.20: awake; the tick ends it
        gps_test_begin();
        return 0;
    }
    if (sp_running(run, SP_CYCLE)) {  // FR-28.6: asleep between readings
        uint32_t s = test_reading();
        send_due_messages();
        if (s) {
            return s;
        }
        test_end(SP_BY_NONE);
        if (!test_next()) {
            tests_over();  // FR-28.11: awake as after a key wake
        } else if (sp_running(run, SP_GPS)) {
            gps_test_begin();  // FR-33.20: awake
        }
        return 0;
    }
    take_reading();
    send_due_messages();
#ifdef TDS3_TEST_SLEEP_S
    return test_sleeps++ < 2 ? TDS3_TEST_SLEEP_S : 0;
#else
    return sp_collect(sampling_sel()).sample_min * 60u;  // FR-23.17
#endif
}

// Runs the task once per boot and, if it asked for a sleep, sleeps. Returns
// only when the task asked to stay awake or the sleep failed.
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

#if !BOARD_HAS_NOTECARD
// FR-35.25, FR-35.2: the panels plugged in (L50), then the display of this
// window from the latest kept reading; L48 with the e-paper found (FR-36.1:
// nothing else logged for it otherwise).
static void display_choose(void)
{
    const ul_modem_t *m = uplink_modem();
    bool epaper, oled;
    lcd_detect(m->epaper, m->oled, &epaper, &oled);
    web_log_displays(!m->epaper ? WL_DISPLAY_OFF : epaper ? WL_DISPLAY_FOUND : WL_DISPLAY_NONE,
                     !m->oled ? WL_DISPLAY_OFF : oled ? WL_DISPLAY_FOUND : WL_DISPLAY_NONE);
    const wx_reading_t *r = weather_newest();
    float t_f = r ? (float)r->temp_centi_c / 100.0f * 9.0f / 5.0f + 32.0f : 0.0f;
    show_on = dl_display_choose(show_on, epaper, oled, r != NULL, t_f);
    show_from_s = uptime_s();
    if (epaper) {
        web_log_display(show_on == DL_SHOW_EPAPER ? "epaper" : "oled", r != NULL, t_f);  // L48
    }
    lcd_use(show_on == DL_SHOW_EPAPER ? LCD_PANEL_EPAPER
            : show_on == DL_SHOW_OLED ? LCD_PANEL_OLED
                                      : LCD_PANEL_NONE);
}

// FR-35.24: the window's wake and its local time, and the gauge's reading.
static void sleep_screen(void)
{
    static const char *const WAKE[] = {[DL_WAKE_NONE] = "power-on", [DL_WAKE_TIMER] = "timer",
                                       [DL_WAKE_KEY] = "key"};
    char when[24] = "clock unset";
    time_t t = time(NULL) - (time_t)(uptime_s() - show_from_s);
    struct tm tm;
    if (time_sync_synced() && localtime_r(&t, &tm)) {
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);
    }
    int mv = -1, pct = 0;
    const max17048_config_t gauge = {.i2c_port = 0, .sda_gpio = BOARD_SENSOR_SDA,
                                     .scl_gpio = BOARD_SENSOR_SCL, .clk_hz = 100000,
                                     .internal_pullup = true};
    max17048_reading_t b;
    if (max17048_start(&gauge) == ESP_OK) {
        if (max17048_read(&b) == ESP_OK) {
            mv = b.mv;
            pct = b.pct;
        }
        (void)max17048_stop();
    }
    static char rows[WL_STATUS_ROWS][WL_STATUS_COLS + 1];
    int n = web_log_status_rows(wake <= DL_WAKE_KEY ? WAKE[wake] : "?", when, mv, pct, rows);
    static char lines[DL_LINES][DL_LINE_MAX];
    dl_sleep_screen(shown, reading_line, lines);
    const char *lp[DL_LINES], *rp[WL_STATUS_ROWS];
    for (int i = 0; i < DL_LINES; i++) {
        lp[i] = lines[i];
    }
    for (int i = 0; i < n; i++) {
        rp[i] = rows[i];
    }
    esp_err_t err = lcd_final(lp, rp, n);
    if (err != ESP_OK) {
        web_log_warn("epaper sleep screen", err);
    }
}
#endif

#ifdef TDS3_TEST_PIN_HOLD
// Test only (sleep-300-hold): the panel stays powered through deep sleep on the
// non-touch board (GPIO 38 is its LED), so its control lines are held at fixed
// levels instead of floating: CS and RST high, the clock and data low.
static const int panel_data[] = TDISPLAY_S3_AMOLED_LCD_DATA;
static const int panel_high[] = {TDISPLAY_S3_AMOLED_LCD_CS, TDISPLAY_S3_AMOLED_LCD_RST};

static esp_err_t hold_line(int pin, int level)
{
    const gpio_config_t cfg = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        err = gpio_set_level(pin, level);
    }
    return err == ESP_OK ? gpio_hold_en(pin) : err;
}

static esp_err_t hold_panel_lines(void)
{
    esp_err_t err = hold_line(TDISPLAY_S3_AMOLED_LCD_SCK, 0);
    for (size_t i = 0; err == ESP_OK && i < 4; i++) {
        err = hold_line(panel_data[i], 0);
    }
    for (size_t i = 0; err == ESP_OK && i < 2; i++) {
        err = hold_line(panel_high[i], 1);
    }
    return err;
}

// A wake keeps the pads held; the panel's bus needs them back.
static void release_panel_lines(void)
{
    gpio_hold_dis(TDISPLAY_S3_AMOLED_LCD_SCK);
    for (size_t i = 0; i < 4; i++) {
        gpio_hold_dis(panel_data[i]);
    }
    for (size_t i = 0; i < 2; i++) {
        gpio_hold_dis(panel_high[i]);
    }
}
#endif

static esp_err_t lcd_sleep_cb(void *arg)
{
#if !BOARD_HAS_NOTECARD
    if (show_on == DL_SHOW_EPAPER && wake != DL_WAKE_TIMER) {
        sleep_screen();  // FR-35.15: a timer wake never draws on the e-paper
        esp_task_wdt_reset();  // the full refresh ran; lcd_sleep waits for it to end
    }
#endif
#ifdef TDS3_TEST_PIN_HOLD
    esp_err_t err = lcd_sleep();  // FR-21.6
    return err == ESP_OK ? hold_panel_lines() : err;
#else
    return lcd_sleep();  // FR-21.6
#endif
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
            redrawn[i] = dl_line_logged(i, shown[i], want[i]);  // FR-7.34
            strcpy(shown[i], want[i]);
        }
    }
    return ESP_OK;
}

// Performs the display actions of one dl_disp_step (D1 – D5).
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

// Once a second: build the screen from the view, draw what changed while the
// panel is OK, and step the display region (D3 – D6). Returns the new state.
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
#if !BOARD_HAS_NOTECARD
    esp_err_t ferr = lcd_flush();  // FR-35.15: the e-paper's refresh, when one is due
    if (ferr != ESP_OK) {
        web_log_warn("epaper refresh", ferr);
    }
#endif
    return step.next;
}

// ── Setup check region (FSD §24) ─────────────────────────────────────────

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// Performs the actions of one dl_hold_step: entering the hold starts the LED
// blink, the key sampler, the countdown and the GPS search; leaving it stops
// them and puts the kept reading back on the screen.
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
        gps_event(UL_GE_START);  // FR-33.16
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
        gps_event(UL_GE_END);  // FR-33.17, FR-33.18
        show_newest();  // line 4 back to the kept reading, line 6 to the add-ons
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

// Each tick of the hold, before the draw: the countdown (FR-24.13) and the live
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

// One station attempt (T2, T3, T8): start the station and arm the 15 s timer
// that turns a missing address into DL_EV_ATTEMPT_FAILED (T6).
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

// The erase of a full history takes seconds; this task is watched (FR-19.1).
static void feed_watchdog(void)
{
    esp_task_wdt_reset();
}

// Performs the action bitmask of one dl_conn_step, in the order the bits are
// tested below; DL_C_RESTART is last and does not return.
static void execute_conn(const app_event_t *e, dl_conn_step_t step)
{
    unsigned a = step.actions;
    if (a & DL_C_STORE_CREDS) {
        esp_err_t err = ESP_OK;
        if (e->creds.ssid[0]) {  // FR-34.2: with WiFi off and none given, none stored
            stored = e->creds;
            err = wifi_net_creds_store(stored.ssid, stored.password);
        }
        if (err != ESP_OK) {
            web_log_warn("credentials store", err);
        }
        err = summary_store();  // FR-25.2
        if (err != ESP_OK) {
            web_log_warn("mail settings store", err);
        }
        err = sampling_store();  // FR-28.3
        if (err != ESP_OK) {
            web_log_warn("setup store", err);
        }
        err = notify_store();  // FR-31.2
        if (err != ESP_OK) {
            web_log_warn("alert settings store", err);
        }
        err = loop_store();  // FR-29.11
        if (err != ESP_OK) {
            web_log_warn("loop store", err);
        }
        err = uplink_store();  // FR-33.2
        if (err != ESP_OK) {
            web_log_warn("uplink store", err);
        }
        web_log_provisioned(stored.ssid);
#if BOARD_HAS_NOTECARD
        // FR-33.12: T3 and T16 go on without a restart, so the Notecard
        // setup and the clock step that boot did with `uplink` `wifi` run now.
        cell_boot(true);
#endif
    }
    if (a & DL_C_STORE_SETTINGS) {  // T14 (FR-26.7)
        esp_err_t err = ESP_OK;
        if (e->creds.ssid[0]) {  // FR-34.2: turning WiFi off never erases the credentials
            stored = e->creds;
            err = wifi_net_creds_store(stored.ssid, stored.password);
        }
        if (err != ESP_OK) {
            web_log_warn("credentials store", err);
        }
        err = summary_store();  // FR-25.16
        if (err != ESP_OK) {
            web_log_warn("mail settings store", err);
        }
        err = sampling_store();  // FR-28.3
        if (err != ESP_OK) {
            web_log_warn("setup store", err);
        }
        err = notify_store();  // FR-31.2, FR-31.14
        if (err != ESP_OK) {
            web_log_warn("alert settings store", err);
        }
        err = loop_store();  // FR-29.11
        if (err != ESP_OK) {
            web_log_warn("loop store", err);
        }
        err = uplink_store();  // FR-33.2
        if (err != ESP_OK) {
            web_log_warn("uplink store", err);
        }
        vTaskDelay(pdMS_TO_TICKS(ANSWER_FLUSH_MS));  // FR-26.8: the result notification first
        ble_event(DL_BEV_WRITTEN);
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
        if (!(a & DL_C_START_ATTEMPT)) {
            esp_err_t err = wifi_net_stop();  // T16: no station follows, the radio goes off (FR-34.8)
            if (err != ESP_OK) {
                web_log_warn("wifi stop", err);
            }
        }
    }
    if (a & DL_C_START_SOFTAP) {
        web_log_portal(softap_ssid);
        esp_err_t err = wifi_net_softap_start(softap_ssid);
        if (err == ESP_OK) {
            err = portal_start_extra(on_submit, form_fields());  // FR-25.1, FR-28.1
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
        web_log_retry(ms, step.n, wifi_net_last_reason());
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
        if (err == ESP_OK) {
            err = history_register(status_http_handle(), status_http_note);  // FR-27.4
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
        err = sampling_erase();  // FR-28.3
        if (err != ESP_OK) {
            web_log_warn("setup erase", err);
        }
        err = notify_erase();  // FR-31.2
        if (err != ESP_OK) {
            web_log_warn("alert settings erase", err);
        }
        err = loop_erase();  // FR-29.11
        if (err != ESP_OK) {
            web_log_warn("loop erase", err);
        }
        err = uplink_erase();  // FR-33.2, FR-33.18: back to wifi, the place erased
        if (err != ESP_OK) {
            web_log_warn("uplink erase", err);
        }
        err = ble_setup_forget();  // FR-26.13
        if (err != ESP_OK) {
            web_log_warn("ble bonds erase", err);
        }
        if (history_available()) {
            err = history_clear(feed_watchdog);  // FR-27.7
            if (err != ESP_OK) {
                web_log_history_failed(err);
            }
        }
    }
    if (a & DL_C_RESTART) {
        if (e->ev == DL_EV_CONFIRM_DEADLINE) {
            web_log_ota_not_confirmed();
        }
        ble_setup_stop();     // FR-26.18: no window survives a restart
        wifi_net_sta_stop();  // leave the AP (deauth) before a planned restart
        vTaskDelay(pdMS_TO_TICKS(LEAVE_MS));
        esp_restart();
    }
}

// One connectivity event: stop the attempt timer where it no longer applies,
// step the region (FSD §5.3), log a state change, perform the actions, and
// pass their consequences to the BLE and setup-check regions.
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
    if (step.actions & DL_C_START_SOFTAP) {
        ble_event(DL_BEV_PROVISIONING);  // FR-26.1
    }
    if (step.actions & DL_C_STORE_CREDS) {
        ble_event(DL_BEV_PROVISIONED);   // FR-26.1: T3 from either path
    }
    if (step.actions & DL_C_MARK_SETUP) {
        hold_event(DL_HEV_MARKED, true);
    }
}

// ── app_main ─────────────────────────────────────────────────────────────

void app_main(void)
{
    web_log_boot();
    time_sync_init();  // FR-22.2, FR-22.4: zone, and whether a sync before a sleep holds
    wake = boot_wake();
#ifdef TDS3_TEST_SLEEP_S
    test_record_boot(wake);
#endif
#ifdef TDS3_TEST_PIN_HOLD
    release_panel_lines();
#endif
    if (wake != DL_WAKE_NONE) {
        web_log_wake(wake == DL_WAKE_TIMER ? "timer" : "button");  // L19
    }
    rtc_boot(wake == DL_WAKE_TIMER);  // FR-39.2: before any reading and cell_boot (FR-39.3)
    deep_sleep_on_prepare(lcd_sleep_cb, NULL);
    uint8_t mac[6];
    wifi_net_base_mac(mac);
    id_softap_ssid(CONFIG_IDENTITY_SOFTAP_SSID_PREFIX, mac, softap_ssid);
    id_hostname(CONFIG_IDENTITY_HOSTNAME_PREFIX, mac, hostname);  // the summary's sender name
    addons_setup();  // §29.2: before any reading or L33
#if !BOARD_HAS_NOTECARD
    if (wake != DL_WAKE_TIMER && !uplink_settings()->cell) {
        modem_link_off();  // FR-35.7: with `uplink` `wifi` the modem stays powered off
    }
#endif
    cell_boot(wake != DL_WAKE_TIMER);  // FR-33.4, FR-33.12, FR-33.14: before the task's sends
    run = sampling_run();
    if (wake != DL_WAKE_TIMER) {
        int was = sampling_running();
        if (was >= 0) {  // FR-28.8: a test a key wake or a reset cut short
            if (!sp_running(run, (sp_test_t)was)) {
                sp_begin(run);  // RTC memory lost with the reset
                sp_start(run, (sp_test_t)was);
            }
            if (was == SP_GPS) {  // FR-33.20: a search a reset cut short ends (FR-33.18)
                gps.on = true;
                gps_event(UL_GE_END);
            }
            test_end(wake == DL_WAKE_KEY ? SP_BY_KEY : SP_BY_RESET);
            tests_over();
        } else if (run->running) {
            sp_begin(run);  // a new selection replaced the tests (T14)
        }
    }
    // A timer wake follows a sleep, which only a provisioned device takes (FR-21.12).
    if (dl_task_due(wake, 0, false, wake == DL_WAKE_TIMER, task_ran, DL_HOLD_NONE)) {
        // FR-21.13: a timer wake runs the task with the panel dark and WiFi off
        // (but for a due summary, FR-25.6), then sleeps again. Only a task that asks to stay awake gets past here.
        run_task_and_sleep();
    }
    // FR-23.3: a reading before the window, unless this boot was a timer wake
    // whose task just took one (the end of a test, FR-28.11).
    bool have_reading = wake == DL_WAKE_TIMER;
    if (wake == DL_WAKE_TIMER && (test_result || run->running)) {
        wake = DL_WAKE_KEY;  // FR-28.11: the tests are over; on as after a key wake
    }
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    if (invalid) {
        web_log_rolled_back(invalid->label);  // L14
    }
    queue = xQueueCreate(16, sizeof(app_event_t));
    esp_err_t herr = history_open();  // FR-27.7: finishes an erase a reset cut short
    if (history_format_erased()) {
        web_log_history_erased_format();  // L36 (FR-29.12)
    }
    if (herr != ESP_OK && herr != ESP_ERR_NOT_FOUND) {
        web_log_history_failed(herr);  // FR-27.9
    }
    if (!have_reading) {
        take_reading();  // FR-23.3: a value for the panel and the page during the window
    }
    view = (dl_view_t){.conn = DL_BOOT, .softap_ssid = softap_ssid,
                       .stored_ssid = stored.ssid, .ip = ip, .hostname = hostname,
                       .reading = reading_line, .prompt = prompt_line,
                       .addons = addons_line};

    // D1/D2: start, clear, and the first draw of the BOOT screen.
    char want[DL_LINES][DL_LINE_MAX];
    dl_screen(&view, want);
#if !BOARD_HAS_NOTECARD
    display_choose();  // FR-35.2: before the panel starts
#endif
    esp_err_t err = lcd_start();
    if (err == ESP_OK) {
        err = draw_changed(want);
    }
    dl_disp_step_t dstep = dl_disp_step(DL_LCD_START, DL_EV_START_DONE, false, err == ESP_OK);
    execute_disp(dstep.actions, err);
    disp = dstep.next;

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

    err = net_ready ? ESP_OK : wifi_net_init(on_net, hostname);
    if (err == ESP_OK) {
        net_ready = true;
        deep_sleep_on_prepare(wifi_stop_cb, NULL);
        deep_sleep_on_prepare(ble_stop_cb, NULL);
    } else {
        web_log_warn("network init", err);
    }
    hold_event(DL_HEV_BOOT, setup_check_pending());  // FR-24.2: NVS is up

    // FR-19.1: this task is watched; the 1 s tick below feeds it.
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "Init complete, running event-driven");

    // T1/T2: credentials read; T15: WiFi off stored (FR-34.4).
    app_event_t cr = {.kind = APP_CONN, .ev = DL_EV_CREDS_READ, .guard = wifi_net_creds_load(stored.ssid, stored.password)};
    if (!uplink_wifi_on()) {
        cr.ev = DL_EV_READ_WIFI_OFF;
    }
    conn_event(&cr);
#if BOARD_HAS_SD_CARD
    if (wake != DL_WAKE_TIMER) {  // FR-35.20: never on a timer wake
        char card[32];
        settings_card_run(card_apply, card, sizeof card);
        web_log_card_settings(card);  // L49
    }
#endif
    err = button_start(web_log_button_held, on_button_reset);  // sampled only after BOOT exits (§18.1)
    if (err != ESP_OK) {
        web_log_warn("button start", err);
    }
    setup_check_on_tap(on_tap);  // FR-28.4, FR-28.8
    err = setup_check_hold_start(DL_BLE_HOLD_MS, on_key_held);  // FR-26.2
    if (err != ESP_OK) {
        web_log_warn("user key start", err);
    }

    // The event loop. Every event but the tick is handled at once; the tick
    // runs the regions in a fixed order: the countdowns and tests that set the
    // screen lines, the owed sends, the draw, then the hold and BLE timeouts
    // and finally the task, which may not return (deep sleep).
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
            web_log_time_synced(NULL);  // L20 (FR-22.5)
            rtc_after_sync(RC_RES_SNTP_MS);  // FR-39.4, FR-39.8
            continue;
        }
        if (e.kind == APP_PHONE_TIME) {
            rtc_phone(&e);  // FR-39.5
            continue;
        }
        if (e.kind == APP_KEY) {
            key_used_s = uptime_s();  // not the first half of a double press (FR-28.4)
            hold_event(DL_HEV_KEY, false);
            continue;
        }
        if (e.kind == APP_TAP) {
            tap(e.guard);
            continue;
        }
        if (e.kind == APP_BLE) {
            ble_event(e.bev);
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
        quick_tick();  // FR-28.6
        gps_test_tick();  // FR-33.20
        if (hold != DL_HOLD_ON) {
            dl_ble_line(&ble, uptime_s(), ble_passkey, prompt_line);  // FR-26.23
            if (!prompt_line[0] && test_result) {
                sp_verdict_line(run, prompt_line);  // FR-28.11
            }
        }
        if (test_mail && (view.conn == DL_ONLINE || mail_via() != UL_VIA_WIFI)) {
            send_test_mail(true);  // FR-28.13, FR-33.6, FR-34.11
        }
        if (test_alert && (view.conn == DL_ONLINE || alert_via() != UL_VIA_WIFI)) {
            send_test_alert(true);  // FR-31.7, FR-33.6, FR-34.11
        }
        cell_tick();  // FR-33.12, FR-33.16, FR-33.19
        disp = disp_tick(disp);
        hold_event(DL_HEV_TICK, false);
        ble_event(DL_BEV_TICK);
        bool provisioned = view.conn != DL_BOOT && view.conn != DL_PROVISIONING;
        if (!task_ran && !run->running && !dl_ble_holds_task(&ble) && dl_task_due(wake, uptime_s() - window_start_s, image_pending_verify(),
                                     provisioned, task_ran, hold)) {
            run_task_and_sleep();  // FR-21.2 – FR-21.4, FR-24.11, FR-24.6: after the window
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
