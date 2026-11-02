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
#define KEY_SLOT "slot"
#define KEY_TRY "try"  // the slot whose send has begun (FR-25.11)
#define MAIL_STACK 8192  // the TLS handshake runs on it

// No capitals, corrections or suggestions from a phone keyboard in these fields.
#define NO_ASSIST " autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\""

// FR-25.1: §25.2's fields, all optional as a group. FR-13.8: each is in the
// group "mail"; the portal's script makes the data-need ones required once any
// of the group is filled.
#define G " data-group=\"mail\""
#define NEED G " data-need"
static const char FORM_HTML[] =
    "<h2>Email summary (optional)</h2>"
    "<p>Fill every starred field, or leave them all empty for no email.</p>"
    "<p><label>SMTP server (Gmail: smtp.gmail.com) "
    "<input name=\"mail_host\" maxlength=\"64\" pattern=\"[A-Za-z0-9.\\-]+\""
    NEED NO_ASSIST "></label></p>"
    "<p><label>Port <select name=\"mail_port\"" NEED "><option value=\"\">-</option>"
    "<option>465</option><option>587</option></select></label></p>"
    "<p><label>Login <input name=\"mail_user\" maxlength=\"64\"" NEED NO_ASSIST "></label></p>"
    "<p><label>Password (Gmail: app password) <input name=\"mail_password\" "
    "type=\"password\" maxlength=\"64\"" NEED "></label></p>"
    "<p><label>From (if not the login) <input name=\"mail_from\" type=\"email\" "
    "maxlength=\"64\"" G NO_ASSIST "></label></p>"
    "<p><label>Send to <input name=\"mail_to\" type=\"email\" maxlength=\"64\"" NEED NO_ASSIST
    "></label></p>"
    "<p><label>Also send to (optional) <input name=\"mail_to2\" type=\"email\" "
    "maxlength=\"64\"" G NO_ASSIST "></label></p>"
    "<p><label>First summary at <input name=\"mail_time\" type=\"time\"" NEED "></label></p>"
    "<p><label>Then every <input name=\"mail_every\" type=\"number\" min=\"1\" max=\"24\""
    NEED "> hours</label></p>";
#undef G
#undef NEED

static sm_settings_t taken;     // from the accepted submission, until T3
static sm_settings_t settings;  // as stored
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
    return sm_parse(body, len, &taken);
}

static const portal_extra_t extra = {.html = FORM_HTML, .take = take};

const char *summary_take(const char *body, size_t len)
{
    return take(body, len);
}

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
    // A new submission starts a new slot count; the sample and average minutes
    // kept here before §28 go too, as sampling_store keeps them from now on.
    err = nvs_erase_all(h);
    if (err == ESP_OK && sm_enabled(&taken)) {
        err = nvs_set_blob(h, KEY_SETTINGS, &taken, sizeof taken);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    settings = taken;
    loaded = err == ESP_OK;
    memset(&taken, 0, sizeof taken);
    return err;
}

esp_err_t summary_erase(void)
{
    memset(&settings, 0, sizeof settings);
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
    nvs_close(h);
}

const sm_settings_t *summary_settings(void)
{
    load();
    return &settings;
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

static job_t job;  // outlives a helper that is still running when the caller gives up

// One email to recipient i (network up), within the budget that ends at
// *deadline (set on the first call, FR-25.14). The text must outlive a helper
// that is still running when the caller gives up.
static mt_reason_t send(const char *hostname, int i, const char *subject, const char *body,
                        int64_t *deadline, time_t *ok_at)
{
    static char date[40];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %z", &t);  // RFC 5322

    // FR-25.14: one 30 s budget for all recipients, from the first connect.
    if (*deadline == 0) {
        *deadline = esp_timer_get_time() + (int64_t)SUMMARY_TIMEOUT_MS * 1000;
    }
    int left_ms = (int)((*deadline - esp_timer_get_time()) / 1000);
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
            if (job.why == MT_OK && !*ok_at) {
                *ok_at = now;
            }
            return job.why;
        }
    }
    return MT_ERR_TIMEOUT;
}

mt_reason_t summary_send(const char *hostname, int i, uint32_t *readings)
{
    *readings = 0;
    if (job.done) {
        return MT_ERR_TIMEOUT;  // a helper that never finished still uses the buffers
    }
    static char body[SM_BODY_MAX], subject[SM_SUBJECT_MAX];
    char stamp[20];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &t);
    sm_subject(hostname, stamp, subject);
    const wx_avgs_t *a = weather_averages();
    closed_now = a->closed;
    uint32_t lines = closed_now - prev_closed, kept = wx_avg_count(a);
    *readings = sm_body(hostname, weather_stats(), weather_newest(), a, lines,
                        lines > kept ? lines - kept : 0, iso, stamp_local, body, sizeof body);
    return send(hostname, i, subject, body, &deadline_us, &sent_at);
}

mt_reason_t summary_send_text(const char *hostname, int i, const char *subject,
                              const char *body)
{
    static int64_t deadline;
    static char subj[SM_SUBJECT_MAX], text[SM_BODY_MAX];
    time_t ok_at = 0;
    if (job.done) {
        return MT_ERR_TIMEOUT;  // a helper that never finished still uses the buffers
    }
    if (i == 0) {
        deadline = 0;  // a new email: its own budget
    }
    strlcpy(subj, subject, sizeof subj);
    strlcpy(text, body, sizeof text);
    load();
    return send(hostname, i, subj, text, &deadline, &ok_at);
}
