#include "summary.h"

#include <stddef.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mail_tx.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "summary_core.h"
#include "weather.h"

#define NVS_NS "mail"
#define KEY_SETTINGS "settings"
#define KEY_COLLECT "collect"  // FR-23.14, kept with email on or off
#define KEY_SLOT "slot"
#define KEY_TRY "try"  // the slot whose send has begun (FR-25.11)
#define MAIL_STACK 8192  // the TLS handshake runs on it

// No capitals, corrections or suggestions from a phone keyboard in these fields.
#define NO_ASSIST " autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\""

// FR-25.1: §25.2's fields, all optional as a group.
static const char FORM_HTML[] =
    "<h2>Email summary (optional)</h2>"
    "<p><label>SMTP server (Gmail: smtp.gmail.com) "
    "<input name=\"mail_host\" maxlength=\"64\"" NO_ASSIST "></label></p>"
    "<p><label>Port <select name=\"mail_port\"><option value=\"\">-</option>"
    "<option>465</option><option>587</option></select></label></p>"
    "<p><label>Login <input name=\"mail_user\" maxlength=\"64\"" NO_ASSIST "></label></p>"
    "<p><label>Password (Gmail: app password) <input name=\"mail_password\" "
    "type=\"password\" maxlength=\"64\"></label></p>"
    "<p><label>From (if not the login) <input name=\"mail_from\" type=\"email\" "
    "maxlength=\"64\"" NO_ASSIST "></label></p>"
    "<p><label>Send to <input name=\"mail_to\" type=\"email\" maxlength=\"64\"" NO_ASSIST "></label></p>"
    "<p><label>Also send to (optional) <input name=\"mail_to2\" type=\"email\" "
    "maxlength=\"64\"" NO_ASSIST "></label></p>"
    "<p><label>First summary at <input name=\"mail_time\" type=\"time\"></label></p>"
    "<p><label>Then every <input name=\"mail_every\" type=\"number\" min=\"1\" max=\"24\"> "
    "hours</label></p>"
    "<h2>Readings</h2>"
    "<p><label>Take a reading every <input name=\"sample_min\" type=\"number\" min=\"1\" "
    "max=\"60\" placeholder=\"10\"> minutes</label></p>"
    "<p><label>Average them over <input name=\"average_min\" type=\"number\" min=\"2\" "
    "max=\"1440\" placeholder=\"60\"> minutes (a whole number of readings, dividing a day)"
    "</label></p>";

static sm_settings_t taken;     // from the accepted submission, until T3
static sm_settings_t settings;  // as stored
static sm_collect_t collect_taken;
static sm_collect_t collect = {SM_SAMPLE_DEFAULT, SM_AVERAGE_DEFAULT};
static bool loaded;
static int64_t due_slot;  // the slot summary_due found due, until summary_done
static int64_t deadline_us;  // FR-25.14: every recipient's send ends by this
static time_t sent_at;       // when a send of this slot first succeeded, else 0

// FR-25.15: how many averages had finished when the previous summary was
// sent, so the next lists only newer ones; a power loss or reset forgets it with
// the averages themselves (FR-23.16).
static RTC_DATA_ATTR uint32_t prev_closed;
static uint32_t closed_now;  // the same, for the summary being sent

static const char *take(const char *body, size_t len)
{
    const char *bad = sm_parse(body, len, &taken);
    return bad ? bad : sm_parse_collect(body, len, &collect_taken);
}

static const portal_extra_t extra = {.html = FORM_HTML, .take = take};

const portal_extra_t *summary_portal_fields(void)
{
    return &extra;
}

static esp_err_t open_rw(nvs_handle_t *h)
{
    return nvs_open(NVS_NS, NVS_READWRITE, h);
}

esp_err_t summary_store(void)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);  // a new submission starts a new slot count
    if (err == ESP_OK && sm_enabled(&taken)) {
        err = nvs_set_blob(h, KEY_SETTINGS, &taken, sizeof taken);
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_COLLECT, &collect_taken, sizeof collect_taken);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    settings = taken;
    collect = collect_taken;
    loaded = err == ESP_OK;
    memset(&taken, 0, sizeof taken);
    return err;
}

esp_err_t summary_erase(void)
{
    memset(&settings, 0, sizeof settings);
    collect = (sm_collect_t){SM_SAMPLE_DEFAULT, SM_AVERAGE_DEFAULT};
    loaded = true;
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
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

static int64_t stored_slot(const char *key)
{
    nvs_handle_t h;
    int64_t slot = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i64(h, key, &slot);
        nvs_close(h);
    }
    return slot;
}

static void store_slot(const char *key, int64_t slot)
{
    nvs_handle_t h;
    if (open_rw(&h) == ESP_OK) {
        if (nvs_set_i64(h, key, slot) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

static void load(void)
{
    if (loaded) {
        return;
    }
    loaded = true;
    memset(&settings, 0, sizeof settings);
    nvs_flash_init();  // a timer wake reaches the task before anything else does
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof settings;
    // Settings stored before mail_from and mail_to2 existed are shorter.
    if (nvs_get_blob(h, KEY_SETTINGS, &settings, &len) != ESP_OK ||
        len < offsetof(sm_settings_t, from)) {
        memset(&settings, 0, sizeof settings);
    }
    sm_collect_t c;
    len = sizeof c;
    if (nvs_get_blob(h, KEY_COLLECT, &c, &len) == ESP_OK && len == sizeof c) {
        collect = c;  // none stored (set up before FR-23.14): the defaults
    }
    nvs_close(h);
}

sm_collect_t summary_collect(void)
{
    load();
    return collect;
}

static int64_t local_now(time_t now)
{
    struct tm t;
    localtime_r(&now, &t);
    return sm_local_seconds(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
                            t.tm_sec);
}

bool summary_due(void)
{
    load();
    time_t now = time(NULL);
    int64_t slot = 0;
    sm_due_t d = sm_due(&settings, now >= WX_TIME_SET_MIN, local_now(now), stored_slot(KEY_SLOT),
                        &slot);
    if (d == SM_NONE) {
        return false;
    }
    if (d == SM_ARM || sm_last_try(slot, stored_slot(KEY_TRY))) {
        store_slot(KEY_SLOT, slot);  // handled from here on: a second reset drops it
    } else {
        store_slot(KEY_TRY, slot);  // a reset before summary_done tries once more
    }
    due_slot = slot;
    deadline_us = 0;
    sent_at = 0;
    return d == SM_SEND;
}

void summary_done(void)
{
    store_slot(KEY_SLOT, due_slot);
    if (sent_at) {  // FR-25.12, FR-25.15: the next summary starts here
        prev_closed = closed_now;
        weather_stats_clear();
    }
}

const char *summary_to(int i)
{
    return sm_to(&settings, i);
}

static void iso(const wx_reading_t *r, char out[WX_ISO_MAX])
{
    weather_iso(r, out);
}

static void stamp_local(int64_t utc, char out[SM_STAMP_MAX])
{
    time_t t = (time_t)utc;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, SM_STAMP_MAX, "%Y-%m-%d %H:%M", &tm);
}

typedef struct {
    mail_tx_server_t server;
    mt_msg_t msg;
    mt_reason_t why;
    SemaphoreHandle_t done;
} job_t;

static void mail_task(void *arg)
{
    job_t *j = arg;
    mail_tx_send(&j->server, &j->msg, &j->why);
    xSemaphoreGive(j->done);
    vTaskDelete(NULL);
}

mt_reason_t summary_send(const char *hostname, int i, uint32_t *readings)
{
    static job_t job;  // outlives a helper that is still running when the caller gives up
    *readings = 0;
    if (job.done) {
        return MT_ERR_TIMEOUT;  // a helper that never finished still uses the buffers
    }
    static char body[SM_BODY_MAX], subject[SM_SUBJECT_MAX];
    char date[40], stamp[20];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %z", &t);  // RFC 5322
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &t);
    sm_subject(hostname, stamp, subject);
    const wx_avgs_t *a = weather_averages();
    closed_now = a->closed;
    uint32_t lines = closed_now - prev_closed, kept = wx_avg_count(a);
    *readings = sm_body(hostname, weather_stats(), weather_newest(), a, lines,
                        lines > kept ? lines - kept : 0, iso, stamp_local, body, sizeof body);

    // FR-25.14: one 30 s budget for all recipients, from the first connect.
    if (deadline_us == 0) {
        deadline_us = esp_timer_get_time() + (int64_t)SUMMARY_TIMEOUT_MS * 1000;
    }
    int left_ms = (int)((deadline_us - esp_timer_get_time()) / 1000);
    if (left_ms <= 0) {
        return MT_ERR_TIMEOUT;
    }
    job = (job_t){
        .server = {.host = settings.host, .port = settings.port, .timeout_ms = left_ms},
        .msg = {.helo = hostname, .user = settings.user, .password = settings.password,
                .from = sm_from(&settings), .to = sm_to(&settings, i), .subject = subject,
                .date = date, .body = body},
        .why = MT_ERR_TIMEOUT,
        .done = xSemaphoreCreateBinary(),
    };
    if (!job.done ||
        xTaskCreate(mail_task, "mail", MAIL_STACK, &job, uxTaskPriorityGet(NULL), NULL) != pdPASS) {
        if (job.done) {
            vSemaphoreDelete(job.done);
            job.done = NULL;
        }
        return MT_ERR_CONNECT;
    }
    bool watched = esp_task_wdt_status(NULL) == ESP_OK;
    // mail_tx ends the session by its own deadline; the margin covers its teardown.
    for (int s = 0; s < left_ms / 1000 + 5; s++) {
        if (watched) {
            esp_task_wdt_reset();
        }
        if (xSemaphoreTake(job.done, pdMS_TO_TICKS(1000)) == pdTRUE) {
            vSemaphoreDelete(job.done);
            job.done = NULL;
            if (job.why == MT_OK && !sent_at) {
                sent_at = now;
            }
            return job.why;
        }
    }
    return MT_ERR_TIMEOUT;
}
