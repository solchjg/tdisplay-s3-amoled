#include "summary.h"

#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_task_wdt.h"
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
#define MAIL_STACK 8192  // the TLS handshake runs on it

// FR-25.1: §25.2's fields, all optional as a group.
static const char FORM_HTML[] =
    "<h2>Email summary (optional)</h2>"
    "<p><label>SMTP server <input name=\"mail_host\" maxlength=\"64\" "
    "placeholder=\"smtp.gmail.com\"></label></p>"
    "<p><label>Port <select name=\"mail_port\"><option value=\"\">-</option>"
    "<option>465</option><option>587</option></select></label></p>"
    "<p><label>Login (sender address) <input name=\"mail_user\" maxlength=\"64\"></label></p>"
    "<p><label>Password (Gmail: app password) <input name=\"mail_password\" "
    "type=\"password\" maxlength=\"64\"></label></p>"
    "<p><label>Send to <input name=\"mail_to\" type=\"email\" maxlength=\"64\"></label></p>"
    "<p><label>First summary at <input name=\"mail_time\" type=\"time\"></label></p>"
    "<p><label>Then every <input name=\"mail_every\" type=\"number\" min=\"1\" max=\"24\"> "
    "hours</label></p>";

static sm_settings_t taken;     // from the accepted submission, until T3
static sm_settings_t settings;  // as stored
static bool loaded;

// FR-25.4: the previous summary's time, so the next covers only newer readings;
// a power loss or reset forgets it with the readings themselves (FR-23.9).
static RTC_DATA_ATTR int64_t prev_sent;

static const char *take(const char *body, size_t len)
{
    return sm_parse(body, len, &taken);
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

static int64_t stored_slot(void)
{
    nvs_handle_t h;
    int64_t slot = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i64(h, KEY_SLOT, &slot);
        nvs_close(h);
    }
    return slot;
}

static void store_slot(int64_t slot)
{
    nvs_handle_t h;
    if (open_rw(&h) == ESP_OK) {
        if (nvs_set_i64(h, KEY_SLOT, slot) == ESP_OK) {
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
    if (nvs_get_blob(h, KEY_SETTINGS, &settings, &len) != ESP_OK || len != sizeof settings) {
        memset(&settings, 0, sizeof settings);
    }
    nvs_close(h);
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
    sm_due_t d = sm_due(&settings, now >= WX_TIME_SET_MIN, local_now(now), stored_slot(), &slot);
    if (d == SM_NONE) {
        return false;
    }
    store_slot(slot);  // handled from here on: sent or dropped, never retried
    return d == SM_SEND;
}

const char *summary_to(void)
{
    return settings.to;
}

static void iso(const wx_reading_t *r, char out[WX_ISO_MAX])
{
    weather_iso(r, out);
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

mt_reason_t summary_send(const char *hostname, uint32_t *readings)
{
    static char body[SM_BODY_MAX], subject[SM_SUBJECT_MAX];
    char date[40], stamp[20];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %z", &t);  // RFC 5322
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &t);
    sm_subject(hostname, stamp, subject);
    *readings = sm_body(hostname, weather_history(), prev_sent, iso, body, sizeof body);

    static job_t job;  // outlives a helper that is still running when the caller gives up
    job = (job_t){
        .server = {.host = settings.host, .port = settings.port,
                   .timeout_ms = SUMMARY_TIMEOUT_MS},
        .msg = {.helo = hostname, .user = settings.user, .password = settings.password,
                .from = settings.user, .to = settings.to, .subject = subject, .date = date,
                .body = body},
        .why = MT_ERR_TIMEOUT,
        .done = xSemaphoreCreateBinary(),
    };
    if (!job.done ||
        xTaskCreate(mail_task, "mail", MAIL_STACK, &job, uxTaskPriorityGet(NULL), NULL) != pdPASS) {
        return MT_ERR_CONNECT;
    }
    bool watched = esp_task_wdt_status(NULL) == ESP_OK;
    // mail_tx ends the session by its own deadline; the margin covers its teardown.
    for (int s = 0; s < SUMMARY_TIMEOUT_MS / 1000 + 5; s++) {
        if (watched) {
            esp_task_wdt_reset();
        }
        if (xSemaphoreTake(job.done, pdMS_TO_TICKS(1000)) == pdTRUE) {
            vSemaphoreDelete(job.done);
            if (job.why == MT_OK) {
                prev_sent = (int64_t)now;
            }
            return job.why;
        }
    }
    return MT_ERR_TIMEOUT;
}
