// Sampling setups (FSD §28), the ESP-IDF side: the selection and the test
// still running, kept in NVS (namespace `sampling`, one blob) so a test
// survives a restart; the progress of a run of tests in RTC memory so it
// survives deep sleep; and the portal form section. The rules are the pure
// sampling_core.c.

#include "sampling.h"

#include <string.h>

#include "esp_attr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS "sampling"
#define KEY_SEL "sel"
#define LEGACY_NS "mail"        // where sample_min and average_min were kept before §28
#define LEGACY_KEY "collect"

typedef struct {
    sp_sel_t sel;
    int8_t running;  // the test running, -1 for none
} stored_t;

const char SAMPLING_FORM_HTML[] =
    "<h2>Readings</h2>"
    "<p><label>Setup <select name=\"setup\">"
    "<option value=\"std\">Standard: every 10 min, 60 min averages</option>"
    "<option value=\"fine\">Frequent: every 1 min, 15 min averages</option>"
    "<option value=\"slow\">Battery saver: every 30 min, 180 min averages</option>"
    "<option value=\"custom\">Custom: the minutes below</option>"
    "</select></label></p>"
    "<p><label>Custom: a reading every <input name=\"sample_min\" type=\"number\" min=\"1\" "
    "max=\"60\" placeholder=\"10\"> minutes</label></p>"
    "<p><label>Custom: average them over <input name=\"average_min\" type=\"number\" min=\"2\" "
    "max=\"1440\" placeholder=\"60\"> minutes (a whole number of readings, dividing a day)"
    "</label></p>"
    "<p>Run first, once:<br>"
    "<label><input type=\"checkbox\" name=\"setup\" value=\"quick\"> Quick check "
    "(12 readings, 10 s apart, 2 min)</label><br>"
    "<label><input type=\"checkbox\" name=\"setup\" value=\"cycle\"> Sleep cycle check "
    "(20 readings, 30 s asleep between, 10 min)</label><br>"
    "<label><input type=\"checkbox\" name=\"setup\" value=\"gps\"> GPS check "
    "(a fix within 20 min, awake; needs cellular and GPS on)</label></p>";

static stored_t taken;  // from the accepted submission, until sampling_store
// As stored, or the defaults.
static stored_t cur = {.sel = {SP_STD, 0, {SM_SAMPLE_DEFAULT, SM_AVERAGE_DEFAULT}}, .running = -1};
static bool loaded;
static RTC_DATA_ATTR sp_run_t run;

const char *sampling_take(const char *body, size_t len)
{
    taken.running = -1;
    return sp_parse(body, len, &taken.sel);
}

static void load(void)
{
    if (loaded) {
        return;
    }
    loaded = true;
    nvs_flash_init();  // a timer wake reaches the task before anything else does
    nvs_handle_t h;
    size_t len = sizeof cur;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        stored_t s;
        esp_err_t err = nvs_get_blob(h, KEY_SEL, &s, &len);
        nvs_close(h);
        if (err == ESP_OK && len == sizeof s && s.sel.normal < SP_NORMALS) {
            cur = s;
            return;
        }
    }
    // FR-28.3: settings stored before §28 kept only the two minutes.
    if (nvs_open(LEGACY_NS, NVS_READONLY, &h) == ESP_OK) {
        sm_collect_t c;
        len = sizeof c;
        if (nvs_get_blob(h, LEGACY_KEY, &c, &len) == ESP_OK && len == sizeof c) {
            cur.sel.normal = SP_CUSTOM;
            cur.sel.custom = c;
        }
        nvs_close(h);
    }
}

const sp_sel_t *sampling_sel(void)
{
    load();
    return &cur.sel;
}

int sampling_running(void)
{
    load();
    return cur.running;
}

static esp_err_t write(const stored_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY_SEL, s, sizeof *s);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t sampling_store(void)
{
    load();
    cur = taken;
    return write(&cur);
}

esp_err_t sampling_erase(void)
{
    loaded = true;
    cur = (stored_t){.sel = {SP_STD, 0, {SM_SAMPLE_DEFAULT, SM_AVERAGE_DEFAULT}}, .running = -1};
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
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

esp_err_t sampling_set(uint8_t tests, int running)
{
    load();
    cur.sel.tests = tests;
    cur.running = (int8_t)running;
    return write(&cur);
}

sp_run_t *sampling_run(void)
{
    return &run;
}
