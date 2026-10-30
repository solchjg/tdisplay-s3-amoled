#include "weather.h"

#include <stdbool.h>
#include <time.h>

#include "bme280.h"
#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "time_sync.h"

// FSD §23.1 and C-23.1: the sensor as wired on this board.
#define SENSOR_SDA_GPIO 43
#define SENSOR_SCL_GPIO 44
#define SENSOR_ADDRESS 0x76
#define SENSOR_CLK_HZ 100000

static const bme280_config_t sensor_cfg = {
    .i2c_port = -1,
    .sda_gpio = SENSOR_SDA_GPIO,
    .scl_gpio = SENSOR_SCL_GPIO,
    .address = SENSOR_ADDRESS,
    .clk_hz = SENSOR_CLK_HZ,
    .internal_pullup = true,
    .settings = BME280_SETTINGS_WEATHER,
};

// FR-23.9: kept in RTC memory, so a deep sleep keeps it and a power loss or
// reset (which reloads RTC data from the image) empties it.
static RTC_DATA_ATTR wx_history_t history;

static bool started;
static weather_note_cb_t note_cb;

esp_err_t weather_sample(wx_reading_t *out)
{
    if (!started) {
        esp_err_t err = bme280_start(&sensor_cfg);
        if (err != ESP_OK) {
            return err;
        }
        started = true;
    }
    bme280_reading_t b;
    esp_err_t err = bme280_read(&b);
    if (err != ESP_OK) {
        bme280_stop();
        started = false;
        return err;
    }
    *out = (wx_reading_t){.time = (int64_t)time(NULL),
                          .temp_centi_c = b.temp_centi_c,
                          .press_q8_pa = b.press_q8_pa,
                          .hum_q10_pct = b.hum_q10_pct,
                          .synced = time_sync_synced()};
    return ESP_OK;
}

esp_err_t weather_read(wx_reading_t *out)
{
    esp_err_t err = weather_sample(out);
    if (err == ESP_OK) {
        wx_add(&history, out);
    }
    return err;
}

const wx_history_t *weather_history(void)
{
    return &history;
}

const wx_reading_t *weather_newest(void)
{
    return wx_get(&history, 0);
}

void weather_iso(const wx_reading_t *r, char out[WX_ISO_MAX])
{
    time_t t = (time_t)r->time;
    struct tm local;
    localtime_r(&t, &local);
    strftime(out, WX_ISO_MAX, "%Y-%m-%dT%H:%M:%S%z", &local);
}

void weather_html(char *out, size_t len)
{
    const wx_reading_t *r = weather_newest();
    char iso[WX_ISO_MAX] = "";
    if (r != NULL) {
        weather_iso(r, iso);
    }
    wx_html(r, iso, out, len);
}

// FR-23.13: the array is sent one element per chunk, so 144 readings need no
// page-sized buffer. Watched while it runs, like every handler (FR-19.2).
static esp_err_t get_readings(httpd_req_t *req)
{
    esp_task_wdt_add(NULL);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr_chunk(req, "{\"readings\":[");
    uint32_t n = wx_count(&history);
    for (uint32_t i = 0; i < n && err == ESP_OK; i++) {
        const wx_reading_t *r = wx_get(&history, i);
        char iso[WX_ISO_MAX], el[WX_JSON_MAX + 1];
        weather_iso(r, iso);
        el[0] = i ? ',' : '\0';
        wx_json(r, iso, el + (i ? 1 : 0), WX_JSON_MAX);
        err = httpd_resp_sendstr_chunk(req, el);
    }
    if (err == ESP_OK) {
        err = httpd_resp_sendstr_chunk(req, "]}");
    }
    if (err == ESP_OK) {
        err = httpd_resp_sendstr_chunk(req, NULL);
    }
    if (note_cb) {
        note_cb("GET", req->uri, 200);
    }
    esp_task_wdt_delete(NULL);
    return err;
}

esp_err_t weather_register(httpd_handle_t server, weather_note_cb_t note)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    note_cb = note;
    static const httpd_uri_t readings = {.uri = "/api/readings", .method = HTTP_GET,
                                         .handler = get_readings};
    return httpd_register_uri_handler(server, &readings);
}
