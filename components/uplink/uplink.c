#include "uplink.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "notecard.h"

#define NVS_NS "uplink"
#define KEY_SETTINGS "settings"
#define KEY_PLACE "place"
#define KEY_WIFI_OFF "wifi_off"  // FR-34.1: its own key, so the settings blob keeps its size
#define KEY_MODEM "modem"        // §35.4: the modem's fields, likewise their own key

// FSD §33.2: the Notecard on the BME280's bus (C-23.1).
#define NC_I2C_PORT 0
#if BOARD_HAS_NOTECARD
#define NC_SDA_GPIO BOARD_NOTECARD_SDA
#define NC_SCL_GPIO BOARD_NOTECARD_SCL
#endif
#define NC_CLK_HZ 100000

// A mail.qo note of a whole summary (SM_BODY_MAX, escaped) with its fields.
#define SM_NOTE_MAX 12288

#define NO_ASSIST " autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\""

typedef struct {
    int32_t lat5, lon5;  // degrees x 100000
    int64_t at;
} place_t;

static ul_settings_t taken;     // from the accepted submission, until it is stored
static ul_settings_t settings;  // as stored
static ul_modem_t taken_m, modem;  // §35.4, the T-SIM7080G build: as taken, as stored
static bool taken_wifi_on = true, wifi_on = true;  // FR-34.1: as taken, as stored
static bool loaded;
static char form[1536];

static void defaults(void)
{
#if BOARD_HAS_NOTECARD
    ul_defaults(&settings);
#else
    ul_modem_defaults(&settings, &modem);
#endif
}

static void load(void)
{
    if (loaded) {
        return;
    }
    loaded = true;
    defaults();
    nvs_flash_init();  // a timer wake reaches the task before anything else does
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof settings;
    if (nvs_get_blob(h, KEY_SETTINGS, &settings, &len) != ESP_OK || len != sizeof settings) {
        defaults();
    }
#if !BOARD_HAS_NOTECARD
    len = sizeof modem;
    if (nvs_get_blob(h, KEY_MODEM, &modem, &len) != ESP_OK || len != sizeof modem) {
        memset(&modem, 0, sizeof modem);
    }
    modem.apn[UL_APN_MAX] = modem.plmn[UL_PLMN_MAX] = '\0';
#endif
    uint8_t off = 0;
    wifi_on = nvs_get_u8(h, KEY_WIFI_OFF, &off) != ESP_OK || !off;
    nvs_close(h);
}

const ul_settings_t *uplink_settings(void)
{
    load();
    return &settings;
}

const ul_modem_t *uplink_modem(void)
{
    load();
    return &modem;
}

bool uplink_wifi_on(void)
{
    load();
    return wifi_on;
}

bool uplink_taken_wifi_on(void)
{
    return taken_wifi_on;
}

const char *uplink_take(const char *body, size_t len)
{
    load();
    const char *bad = ul_parse_wifi(body, len, wifi_on, &taken_wifi_on);
#if BOARD_HAS_NOTECARD
    return bad ? bad : ul_parse(body, len, &settings, &taken);
#else
    return bad ? bad : ul_parse_modem(body, len, &settings, &modem, &taken, &taken_m);
#endif
}

const char *uplink_form_html(void)
{
    if (form[0]) {
        return form;
    }
    load();
    const ul_settings_t *s = &settings;
#if BOARD_HAS_NOTECARD
    char every[8];
    snprintf(every, sizeof every, "%u", (unsigned)s->cell_every);
    snprintf(form, sizeof form,
             "<h2>Cellular uplink (optional)</h2>"
             "<p>Without WiFi at the site, alerts and email go out as notes through a Blues "
             "Notecard.</p>"
             "<p><label>Uplink <select name=\"uplink\"><option%s>wifi</option>"
             "<option%s>cell</option></select></label></p>"
             "<p><label>Notehub ProductUID <input name=\"nh_product\" maxlength=\"64\" "
             "pattern=\"[A-Za-z0-9.:_\\-]+\" placeholder=\"com.example.you:weather\" "
             "value=\"%s\"" NO_ASSIST "></label></p>"
             "<p><label>Sync every <input name=\"cell_every\" type=\"number\" min=\"10\" "
             "max=\"1440\" placeholder=\"60\" value=\"%s\"> minutes</label></p>"
             "<p><label>Email over cellular <select name=\"cell_mail\"><option%s>off</option>"
             "<option%s>route</option></select></label></p>"
             "<p><label>GPS fix in the setup check <select name=\"gps\"><option%s>off</option>"
             "<option%s>on</option></select></label></p>",
             s->cell ? "" : " selected", s->cell ? " selected" : "", s->product, every,
             s->mail_route ? "" : " selected", s->mail_route ? " selected" : "",
             s->gps ? "" : " selected", s->gps ? " selected" : "");
    return form;
#else
    // §35.4: the board's modem; no Notehub fields.
    snprintf(form, sizeof form,
             "<h2>Cellular uplink (optional)</h2>"
             "<p>Without WiFi at the site, alerts and email go out through the board's LTE-M "
             "modem.</p>"
             "<p><label>Uplink <select name=\"uplink\"><option%s>wifi</option>"
             "<option%s>cell</option></select></label></p>"
             "<p><label>APN (from the SIM's provider) <input name=\"apn\" maxlength=\"63\" "
             "pattern=\"[A-Za-z0-9.\\-]+\" placeholder=\"hologram\" value=\"%s\"" NO_ASSIST
             "></label></p>"
             "<p><label>Network (MCC+MNC, empty for automatic) <input name=\"cell_plmn\" "
             "maxlength=\"6\" pattern=\"[0-9]{5,6}\" placeholder=\"310410\" value=\"%s\""
             NO_ASSIST "></label></p>"
             "<p><label>Email over cellular <select name=\"cell_mail\"><option%s>on</option>"
             "<option%s>off</option></select></label></p>"
             "<p><label>GPS fix in the setup check <select name=\"gps\"><option%s>off</option>"
             "<option%s>on</option></select></label></p>",
             s->cell ? "" : " selected", s->cell ? " selected" : "", modem.apn, modem.plmn,
             s->mail_route ? " selected" : "", s->mail_route ? "" : " selected",
             s->gps ? "" : " selected", s->gps ? " selected" : "");
    return form;
#endif
}

static esp_err_t store_settings(const ul_settings_t *s, const ul_modem_t *m, bool erase_place)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (erase_place) {
        err = nvs_erase_all(h);
    }
    if (err == ESP_OK && s) {
        err = nvs_set_blob(h, KEY_SETTINGS, s, sizeof *s);
    }
    if (err == ESP_OK && s) {
        err = nvs_set_u8(h, KEY_WIFI_OFF, taken_wifi_on ? 0 : 1);
    }
#if !BOARD_HAS_NOTECARD
    if (err == ESP_OK && m) {
        err = nvs_set_blob(h, KEY_MODEM, m, sizeof *m);
    }
#else
    (void)m;
#endif
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t uplink_store(void)
{
    esp_err_t err = store_settings(&taken, &taken_m, false);
    settings = taken;
    modem = taken_m;
    wifi_on = taken_wifi_on;
    loaded = err == ESP_OK;
    form[0] = '\0';
    return err;
}

esp_err_t uplink_erase(void)
{
    defaults();
    wifi_on = taken_wifi_on = true;  // FR-34.1: a factory reset turns WiFi back on
    loaded = true;
    form[0] = '\0';
    return store_settings(NULL, NULL, true);
}

bool uplink_place(double *lat, double *lon, int64_t *at)
{
    nvs_flash_init();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    place_t p;
    size_t len = sizeof p;
    bool ok = nvs_get_blob(h, KEY_PLACE, &p, &len) == ESP_OK && len == sizeof p;
    nvs_close(h);
    if (ok) {
        *lat = p.lat5 / 100000.0;
        *lon = p.lon5 / 100000.0;
        *at = p.at;
    }
    return ok;
}

esp_err_t uplink_place_store(double lat, double lon, int64_t at)
{
    place_t p = {.lat5 = (int32_t)(lat * 100000.0 + (lat >= 0 ? 0.5 : -0.5)),
                 .lon5 = (int32_t)(lon * 100000.0 + (lon >= 0 ? 0.5 : -0.5)),
                 .at = at};
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY_PLACE, &p, sizeof p);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// ------------------------------------------------------------- the Notecard

static char line[256];  // a request without a body
static char rsp[512];   // an answer (only short fields are read)
static bool started, ready;
static const char *start_why, *start_req;

// One request; NULL on success, else the L44 reason.
static const char *ask(const char *req)
{
    esp_err_t err = notecard_request(req, rsp, sizeof rsp);
    if (err == ESP_ERR_INVALID_SIZE) {
        err = ESP_OK;  // the start of a long answer is enough
    }
    return ul_fail_reason(err == ESP_OK, rsp);
}

static const char *ask_name(const char *name)
{
    return nc_req(line, sizeof line, name) ? ask(line) : "err";
}

const char *uplink_start(const char **req)
{
    if (started) {
        if (req) {
            *req = start_req;
        }
        return start_why;
    }
    started = true;
    load();
#if !BOARD_HAS_NOTECARD
    // No Notecard on this board (§35.3); its modem uplink comes with Phase 17 step 3.
    start_req = "card.version";
    start_why = "timeout";
    if (req) {
        *req = start_req;
    }
    return start_why;
#else
    const notecard_config_t cfg = {.i2c_port = NC_I2C_PORT,
                                   .sda_gpio = NC_SDA_GPIO,
                                   .scl_gpio = NC_SCL_GPIO,
                                   .address = NOTECARD_I2C_ADDR,
                                   .clk_hz = NC_CLK_HZ,
                                   .internal_pullup = true,
                                   .timeout_s = UL_ANSWER_S};
    start_req = "card.version";
    if (notecard_start(&cfg) != ESP_OK) {
        start_why = "timeout";
    } else {
        start_why = ask_name("card.version");  // FR-33.4: is it there?
    }
    if (!start_why) {
        start_req = "hub.set";
        const char *why = ask_name("hub.get");
        if (why || !nc_hub_matches(rsp, settings.product, settings.cell_every, UL_INBOUND_MIN)) {
            start_why = nc_req_hub_set(line, sizeof line, settings.product, settings.cell_every,
                                       UL_INBOUND_MIN)
                            ? ask(line)
                            : "err";
        }
    }
    if (!start_why) {
        start_req = "card.transport";
        const char *why = ask_name("card.transport");
        if (why || !nc_transport_is(rsp, "cell")) {  // FR-33.14
            start_why = nc_req_transport(line, sizeof line, "cell") ? ask(line) : "err";
        }
    }
    ready = start_why == NULL;
    if (req) {
        *req = start_req;
    }
    return start_why;
#endif
}

bool uplink_ready(void)
{
    return ready;
}

const char *uplink_note(const char *file, const char *body, bool sync)
{
    static char req[SM_NOTE_MAX];
    if (!ready) {
        return "timeout";
    }
    return nc_req_note_add(req, sizeof req, file, body, sync) ? ask(req) : "err";
}

bool uplink_time(int64_t *epoch_s)
{
    double t;
    if (!ready || ask_name("card.time") || !nc_num(rsp, "time", &t)) {
        return false;
    }
    *epoch_s = (int64_t)t;
    return true;
}

const char *uplink_hub_sync(void)
{
    return ready ? ask_name("hub.sync") : "timeout";
}

const char *uplink_location_mode(const char *mode, const double *lat, const double *lon)
{
    if (!ready) {
        return "timeout";
    }
    return nc_req_location_mode(line, sizeof line, mode, lat, lon) ? ask(line) : "err";
}

const char *uplink_location(void)
{
    return ready && !ask_name("card.location") ? rsp : NULL;
}
