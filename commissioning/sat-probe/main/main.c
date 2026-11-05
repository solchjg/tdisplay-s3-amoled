// Commissioning aid, not product code: the satellite trial's sender (stages 2
// and 3 of the "Satellite data trial plan"; no FSD section yet). Once after
// reset it
//   1. checks the Notecard on the main bus (GPIO 43/44) and prints
//      card.version, card.wireless and card.time;
//   2. with a ProductUID set (menuconfig, "Satellite probe"), sends hub.set
//      where the Notecard differs;
//   3. sets card.transport to cell (stage 2) or ntn (stage 3) where it differs;
//   4. sends the compact template of day.qo (57 bytes, or 61 with min/max)
//      and, with PROBE_OVERSIZE, of big.qo (280 bytes);
//   5. packs each sample day compiled in from tools/sat_ref.py (sample_days.h)
//      with the sat_day core, prints the body and the raw size, and adds it to
//      day.qo; with PROBE_OVERSIZE one note to big.qo;
//   6. forces one sync, waits for its end (up to 15 min over satellite) and
//      prints how long it took and card.usage.get before and after.
// Then it prints the signal bars every 60 s. Each Notehub day.qo event, the
// route log and ThingsBoard are checked by hand (ThingsBoard setup for the
// satellite trial).

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "notecard.h"
#include "sample_days.h"
#include "sat_day.h"
#include "sdkconfig.h"

#define PIN_SDA 43
#define PIN_SCL 44
#define CELL_EVERY_MIN 60
#define INBOUND_MIN 1440
#define SYNC_LIMIT_S (15 * 60)
#define BIG_FILE "big.qo"
#define BIG_PORT 11
#define BIG_VALUES 140  // 140 × float16 = 280 bytes, over Skylo's 256

#ifdef CONFIG_PROBE_NTN
#define TRANSPORT "ntn"
#else
#define TRANSPORT "cell"
#endif
#ifdef CONFIG_PROBE_MINMAX
#define MINMAX true
#else
#define MINMAX false
#endif
#ifdef CONFIG_PROBE_OVERSIZE
#define OVERSIZE 1
#else
#define OVERSIZE 0
#endif

static i2c_master_bus_handle_t bus;
static char req[4096];
static char rsp[1024];
static char body[3072];

// Sends one request line, prints the answer and how long it took; true when
// an answer without "err" came back.
static bool ask(const char *what, const char *line)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = notecard_request(line, rsp, sizeof rsp);
    long ms = (long)((esp_timer_get_time() - t0) / 1000);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        printf("satprobe: %s failed=%s ms=%ld\n", what, esp_err_to_name(err), ms);
        return false;
    }
    printf("satprobe: %s ms=%ld %s\n", what, ms, rsp);
    return !nc_err(rsp, NULL, 0);
}

static bool ask_name(const char *name)
{
    char line[64];
    return nc_req(line, sizeof line, name) && ask(name, line);
}

static void setup_hub(void)
{
    const char *product = CONFIG_PROBE_PRODUCT;
    char line[256];
    if (product[0] == '\0') {
        printf("satprobe: no ProductUID set: hub.set left alone\n");
    } else if (!nc_product_valid(product)) {
        printf("satprobe: ProductUID '%s' invalid CHECK\n", product);
    } else if (ask_name("hub.get") && nc_hub_matches(rsp, product, CELL_EVERY_MIN, INBOUND_MIN)) {
        printf("satprobe: hub.set not needed\n");
    } else if (nc_req_hub_set(line, sizeof line, product, CELL_EVERY_MIN, INBOUND_MIN)) {
        ask("hub.set", line);
    }
    if (ask_name("card.transport") && nc_transport_is(rsp, TRANSPORT)) {
        printf("satprobe: transport already %s\n", TRANSPORT);
    } else if (nc_req_transport(line, sizeof line, TRANSPORT)) {
        ask("card.transport " TRANSPORT, line);
    }
}

static void templates(void)
{
    if (sd_template_json(MINMAX, req, sizeof req - 1) < 0) {
        printf("satprobe: day.qo template does not fit CHECK\n");
        return;
    }
    strcat(req, "\n");
    ask("note.template " SD_FILE, req);
    if (!OVERSIZE) {
        return;
    }
    int o = snprintf(req, sizeof req,
                     "{\"req\":\"note.template\",\"file\":\"%s\",\"format\":\"compact\","
                     "\"port\":%d,\"body\":{",
                     BIG_FILE, BIG_PORT);
    for (int i = 1; i <= BIG_VALUES; i++) {
        o += snprintf(req + o, sizeof req - (size_t)o, "%s\"v%d\":12.1", i > 1 ? "," : "", i);
    }
    snprintf(req + o, sizeof req - (size_t)o, "}}\n");
    ask("note.template " BIG_FILE, req);
}

static void send_days(void)
{
    for (size_t k = 0; k < sizeof sp_days / sizeof sp_days[0]; k++) {
        const sp_day_t *day = &sp_days[k];
        sd_day_t d;
        sd_begin(&d, sd_window_start(day->now));
        for (uint16_t i = 0; i < day->n; i++) {
            sd_add(&d, day->rows[i].t, day->rows[i].v);
        }
        sd_note_t n;
        sd_finish(&d, &n);
        uint8_t raw[SD_BYTES_MINMAX];
        size_t raw_len = sd_raw(&n, MINMAX, raw, sizeof raw);
        int blen = sd_body_json(&n, MINMAX, body, sizeof body);
        size_t len = blen > 0 ? nc_req_note_add(req, sizeof req, SD_FILE, body, false) : 0;
        printf("satprobe: day \"%s\" t0=%" PRIu32 " miss=%06" PRIx32 " too_big=%06" PRIx32
               " raw_bytes=%u body=%s\n",
               day->name, n.t0, n.miss, n.too_big, (unsigned)raw_len, blen > 0 ? body : "-");
        if (len == 0) {
            printf("satprobe: day \"%s\" does not fit CHECK\n", day->name);
            continue;
        }
        ask("note.add " SD_FILE, req);
    }
    if (!OVERSIZE) {
        return;
    }
    int o = snprintf(body, sizeof body, "{");
    for (int i = 1; i <= BIG_VALUES; i++) {
        o += snprintf(body + o, sizeof body - (size_t)o, "%s\"v%d\":%d.5", i > 1 ? "," : "", i, i);
    }
    snprintf(body + o, sizeof body - (size_t)o, "}");
    if (nc_req_note_add(req, sizeof req, BIG_FILE, body, false)) {
        ask("note.add " BIG_FILE " (280 bytes)", req);
    }
}

static void sync_and_wait(void)
{
    ask_name("card.usage.get");
    int64_t t0 = esp_timer_get_time();
    ask_name("hub.sync");
    bool ended = false;
    // Ended once the answer reports a sync completed since this one was asked
    // ("completed": seconds since the last sync ended, no "sync":true pending),
    // or says {sync-end}. The Notecard drops "status" once the modem is off.
    for (int s = 0; s < SYNC_LIMIT_S && !ended; s += 10) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!ask_name("hub.sync.status")) {
            continue;
        }
        char status[96];
        double completed;
        long elapsed = (long)((esp_timer_get_time() - t0) / 1000000);
        bool pending = strstr(rsp, "\"sync\":true") != NULL;
        ended = (nc_str(rsp, "status", status, sizeof status) &&
                 strstr(status, "{sync-end}") != NULL) ||
                (!pending && nc_num(rsp, "completed", &completed) && completed <= elapsed);
    }
    printf("satprobe: sync %s after_s=%lld %s\n", ended ? "ended" : "did not end",
           (long long)((esp_timer_get_time() - t0) / 1000000), ended ? "ok" : "CHECK");
    ask_name("card.usage.get");
    printf("satprobe: now check Notehub events (day.qo), the route log and ThingsBoard\n");
}

void app_main(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = 0,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    printf("satprobe: bus=%s transport=%s minmax=%d oversize=%d days=%u\n",
           esp_err_to_name(i2c_new_master_bus(&bc, &bus)), TRANSPORT, MINMAX, OVERSIZE,
           (unsigned)(sizeof sp_days / sizeof sp_days[0]));
    bool nc = i2c_master_probe(bus, NOTECARD_I2C_ADDR, 50) == ESP_OK;
    printf("satprobe: notecard=%s\n", nc ? "yes" : "no CHECK");
    notecard_config_t cfg = {.i2c_port = 0, .address = NOTECARD_I2C_ADDR, .clk_hz = 100000,
                             .timeout_s = 10};
    esp_err_t err = notecard_start(&cfg);
    printf("satprobe: start=%s\n", esp_err_to_name(err));
    if (err == ESP_OK) {
        ask_name("card.version");
        ask_name("card.wireless");
        ask_name("card.time");
        setup_hub();
        templates();
        send_days();
        sync_and_wait();
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        if (err == ESP_OK) {
            ask_name("card.wireless");
        }
    }
}
