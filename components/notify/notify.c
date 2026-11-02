#include "notify.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ntfy_tx.h"

#define NVS_NS "ntfy"
#define KEY_SETTINGS "settings"
#define NTFY_STACK 8192  // an HTTPS handshake runs on it

#define NO_ASSIST " autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\""

static nf_settings_t taken;     // from the accepted submission, until it is stored
static nf_settings_t settings;  // as stored
static bool loaded;
static RTC_DATA_ATTR nf_state_t state;  // FR-31.5
static int64_t deadline_us;             // FR-31.11: the run's alerts end by this

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

const nf_settings_t *notify_settings(void)
{
    load();
    return &settings;
}

const char *notify_take(const char *body, size_t len)
{
    load();
    return nf_parse(body, len, &settings, &taken);
}

static char form[2048];

const char *notify_form_html(void)
{
    if (form[0]) {
        return form;
    }
    load();
    char topic[NT_TOPIC_MAX + 1];
    if (settings.topic[0]) {
        strlcpy(topic, settings.topic, sizeof topic);
    } else {  // FR-31.3: hard to guess
        strlcpy(topic, "tds3-", sizeof topic);
        for (int i = 0; i < 12; i++) {
            topic[5 + i] = (char)('a' + esp_random() % 26);
        }
        topic[17] = '\0';
    }
    char lim[NF_QTY][2][12];
    for (int q = 0; q < NF_QTY; q++) {
        for (int hi = 0; hi < 2; hi++) {
            nf_limit_text(&settings, (nf_qty_t)q, hi, lim[q][hi], sizeof lim[q][hi]);
        }
    }
    snprintf(form, sizeof form,
             "<h2>Push alerts (optional)</h2>"
             "<p>Alerts go to an ntfy server when a reading passes a limit, the sensor or "
             "history fails, or a test ends.</p>"
             "<p><label>Alerts <select name=\"ntfy\"><option%s>off</option>"
             "<option%s>on</option></select></label></p>"
             "<p><label>ntfy server (e.g. http://192.168.2.37:8080) <input name=\"ntfy_url\" "
             "maxlength=\"96\" pattern=\"https?://[A-Za-z0-9.\\-]+(:[0-9]{1,5})?\" value=\"%s\""
             NO_ASSIST "></label></p>"
             "<p><label>Topic <input name=\"ntfy_topic\" maxlength=\"64\" "
             "pattern=\"[A-Za-z0-9_\\-]+\" value=\"%s\"" NO_ASSIST "></label></p>"
             "<p><label>Access token (optional) <input name=\"ntfy_token\" type=\"password\" "
             "maxlength=\"64\"" NO_ASSIST "></label></p>"
             "<p>Limits, empty for none:</p>"
             "<p><label>Temperature below <input name=\"alert_t_lo\" type=\"number\" "
             "step=\"0.1\" min=\"-40\" max=\"185\" value=\"%s\"></label> "
             "<label>or above <input name=\"alert_t_hi\" type=\"number\" step=\"0.1\" "
             "min=\"-40\" max=\"185\" value=\"%s\"></label> F</p>"
             "<p><label>Humidity below <input name=\"alert_rh_lo\" type=\"number\" min=\"0\" "
             "max=\"100\" value=\"%s\"></label> <label>or above <input name=\"alert_rh_hi\" "
             "type=\"number\" min=\"0\" max=\"100\" value=\"%s\"></label> %%</p>"
             "<p><label>Pressure below <input name=\"alert_p_lo\" type=\"number\" "
             "step=\"0.01\" min=\"300\" max=\"1100\" value=\"%s\"></label> "
             "<label>or above <input name=\"alert_p_hi\" type=\"number\" step=\"0.01\" "
             "min=\"300\" max=\"1100\" value=\"%s\"></label> mbar</p>",
             settings.on ? "" : " selected", settings.on ? " selected" : "", settings.url,
             topic, lim[0][0], lim[0][1], lim[1][0], lim[1][1], lim[2][0], lim[2][1]);
    return form;
}

static esp_err_t store_blob(const nf_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK && s) {
        err = nvs_set_blob(h, KEY_SETTINGS, s, sizeof *s);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t notify_store(void)
{
    esp_err_t err = store_blob(&taken);
    settings = taken;
    loaded = err == ESP_OK;
    memset(&taken, 0, sizeof taken);
    memset(&state, 0, sizeof state);  // FR-31.14: every condition starts as normal
    form[0] = '\0';
    return err;
}

esp_err_t notify_erase(void)
{
    memset(&settings, 0, sizeof settings);
    memset(&state, 0, sizeof state);
    loaded = true;
    form[0] = '\0';
    return store_blob(NULL);
}

nf_state_t *notify_state(void)
{
    return &state;
}

void notify_begin(void)
{
    deadline_us = 0;
}

typedef struct {
    ntfy_tx_server_t server;
    nt_msg_t msg;
    nt_reason_t why;
    SemaphoreHandle_t done;
} job_t;

static job_t job;  // outlives a helper that is still running when the caller gives up
static char title_buf[NT_TITLE_MAX + 1], body_buf[NT_BODY_MAX + 1], click[NT_CLICK_MAX + 1];

static void ntfy_task(void *arg)
{
    job_t *j = arg;
    ntfy_tx_send(&j->server, &j->msg, &j->why);
    xSemaphoreGive(j->done);
    vTaskDelete(NULL);
}

nt_reason_t notify_send(const char *hostname, const char *title, const char *body, bool good)
{
    if (job.done) {  // a helper the last call gave up on: free once it has finished
        if (xSemaphoreTake(job.done, 0) != pdTRUE) {
            return NT_ERR_TIMEOUT;  // still running: it uses the buffers
        }
        vSemaphoreDelete(job.done);
        job.done = NULL;
    }
    load();
    if (deadline_us == 0) {
        deadline_us = esp_timer_get_time() + (int64_t)NOTIFY_TIMEOUT_MS * 1000;
    }
    int left_ms = (int)((deadline_us - esp_timer_get_time()) / 1000);
    if (left_ms <= 0) {
        return NT_ERR_TIMEOUT;
    }
    strlcpy(title_buf, title, sizeof title_buf);
    strlcpy(body_buf, body, sizeof body_buf);
    snprintf(click, sizeof click, "http://%s.local/", hostname);
    job = (job_t){
        .server = {.url = settings.url, .topic = settings.topic, .token = settings.token,
                   .timeout_ms = (uint32_t)left_ms},
        .msg = {.title = title_buf, .body = body_buf, .priority = nf_priority(good),
                .tags = nf_tags(good), .click = click},
        .why = NT_ERR_TIMEOUT,
        .done = xSemaphoreCreateBinary(),
    };
    if (!job.done ||
        xTaskCreate(ntfy_task, "ntfy", NTFY_STACK, &job, uxTaskPriorityGet(NULL), NULL) != pdPASS) {
        if (job.done) {
            vSemaphoreDelete(job.done);
            job.done = NULL;
        }
        return NT_ERR_CONNECT;
    }
    bool watched = esp_task_wdt_status(NULL) == ESP_OK;
    // The client's timeout bounds each step; the budget bounds the wait here.
    for (int s = 0; s < left_ms / 1000 + 1; s++) {
        if (watched) {
            esp_task_wdt_reset();
        }
        if (xSemaphoreTake(job.done, pdMS_TO_TICKS(1000)) == pdTRUE) {
            vSemaphoreDelete(job.done);
            job.done = NULL;
            return job.why;
        }
    }
    return NT_ERR_TIMEOUT;
}
