#include "summary.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_attr.h"
#include "mail_core.h"
#include "nvs.h"
#include "summary_core.h"
#include "weather.h"

#define NVS_NS "mail"
#define NVS_KEY "settings"
#define SEND_TIMEOUT_MS (30 * 1000)  // FR-25.7
#define MSG_MAX 2048

static RTC_DATA_ATTR sm_settings_t settings;  // the stored settings, kept across deep sleep
static RTC_DATA_ATTR sm_state_t state;        // FR-25.3: due times answered
static sm_settings_t submitted;               // what the portal's check accepted

// The §25.2 fields; the port's empty first option keeps an untouched form all
// empty (email off, FR-25.1). Nothing echoes a value back, so nothing is escaped.
static const char FORM[] =
    "<h2>Summary email (optional)</h2>"
    "<p><label>SMTP server <input name=\"mail_host\" maxlength=\"64\" "
    "placeholder=\"smtp.gmail.com\"></label></p>"
    "<p><label>Port <select name=\"mail_port\"><option value=\"\"></option><option>465</option><option>587</option>"
    "</select></label></p>"
    "<p><label>User (sender) <input name=\"mail_user\" maxlength=\"64\"></label></p>"
    "<p><label>Password <input name=\"mail_password\" type=\"password\" maxlength=\"64\">"
    "</label></p>"
    "<p><label>Send to <input name=\"mail_to\" type=\"email\" maxlength=\"64\"></label></p>"
    "<p><label>First summary <input name=\"mail_time\" type=\"time\"></label></p>"
    "<p><label>Every (hours) <input name=\"mail_every\" type=\"number\" min=\"1\" max=\"24\">"
    "</label></p>";

static const char *check(const char *body, size_t len)
{
    return sm_parse(body, len, &submitted);
}

const portal_extra_t summary_portal = {.form_html = FORM, .check = check};

esp_err_t summary_load(void)
{
    nvs_handle_t h;
    sm_settings_t s = {0};
    size_t len = sizeof s;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, NVS_KEY, &s, &len);
        nvs_close(h);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;  // nothing stored: email off
    }
    settings = err == ESP_OK && len == sizeof s ? s : (sm_settings_t){0};
    return err;
}

static esp_err_t store(const sm_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = s->enabled ? nvs_set_blob(h, NVS_KEY, s, sizeof *s) : nvs_erase_key(h, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        settings = *s;
        state = (sm_state_t){0};
    }
    return err;
}

esp_err_t summary_store_submitted(void)
{
    esp_err_t err = store(&submitted);
    memset(&submitted, 0, sizeof submitted);
    return err;
}

esp_err_t summary_erase(void)
{
    static const sm_settings_t off = {0};
    return store(&off);
}

static void local(time_t t, struct tm *tm)
{
    localtime_r(&t, tm);
}

bool summary_due(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    local(tv.tv_sec, &tm);
    return sm_due(&state, &settings, tv.tv_sec, tm.tm_hour * 60 + tm.tm_min, tm.tm_sec);
}

const char *summary_recipient(void)
{
    return settings.to;
}

esp_err_t summary_send(const char *hostname, uint32_t *count, mail_tx_result_t *why)
{
    static char body[768], msg[MSG_MAX];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    char sent[20], from[20] = "", to[20] = "", subject[96], date[40];

    sm_stats_t st;
    sm_stats(weather_history(), state.last_sent, &st);
    local(tv.tv_sec, &tm);
    strftime(sent, sizeof sent, "%Y-%m-%d %H:%M", &tm);
    strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %z", &tm);  // RFC 5322 §3.3
    if (st.count > 0) {
        local((time_t)st.oldest->time, &tm);
        strftime(from, sizeof from, "%Y-%m-%d %H:%M", &tm);
        local((time_t)st.newest->time, &tm);
        strftime(to, sizeof to, "%Y-%m-%d %H:%M", &tm);
    }
    sm_subject(hostname, sent, subject, sizeof subject);
    sm_body(&st, hostname, from, to, body, sizeof body);
    size_t n = mc_message(msg, sizeof msg, date, settings.user, settings.to, subject, body);
    *count = st.count;
    if (n == 0) {
        *why = MAIL_TX_REFUSED;
        return ESP_ERR_NO_MEM;
    }
    const mail_tx_config_t cfg = {.host = settings.host, .port = settings.port,
                                  .starttls = settings.port == 587, .helo = hostname,
                                  .user = settings.user, .password = settings.password,
                                  .from = settings.user, .to = settings.to, .msg = msg,
                                  .msg_len = n, .timeout_ms = SEND_TIMEOUT_MS};
    esp_err_t err = mail_tx_send(&cfg, why);
    if (err == ESP_OK) {
        state.last_sent = tv.tv_sec;
    }
    return err;
}
