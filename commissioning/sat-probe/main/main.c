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
//   6. forces one sync, waits for its end (up to 15 min, 20 over satellite) and
//      prints how long it took and card.usage.get before and after.
// Then it prints the signal bars every 60 s. Each Notehub day.qo event, the
// route log and ThingsBoard are checked by hand (ThingsBoard setup for the
// satellite trial).
// Each step also shows on the AMOLED, so a run on batteries outdoors needs no
// laptop. The ntn build waits for a press of the user key (GPIO 21) or BOOT
// before step 5, so powering up indoors queues nothing; after the sync a press
// forces another sync of whatever is still queued, without adding notes.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "notecard.h"
#include "sample_days.h"
#include "sat_day.h"
#include "screen.h"
#include "sdkconfig.h"
#include "tdisplay_s3_amoled.h"

#define PIN_SDA 43
#define PIN_SCL 44
#define CELL_EVERY_MIN 60
#define INBOUND_MIN 1440
#define BIG_FILE "big.qo"
#define BIG_PORT 11
#define BIG_VALUES 140  // 140 × float16 = 280 bytes, over Skylo's 256

#ifdef CONFIG_PROBE_NTN
#define TRANSPORT "ntn"
#define SYNC_LIMIT_S (20 * 60)  // a GPS fix and Skylo registration come first
#define WAIT_KEY 1
#else
#define TRANSPORT "cell"
#define SYNC_LIMIT_S (15 * 60)
#define WAIT_KEY 0
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
    size_t days = sizeof sp_days / sizeof sp_days[0];
    for (size_t k = 0; k < days; k++) {
        const sp_day_t *day = &sp_days[k];
        screen_line(1, SCREEN_YELLOW, "SENDING day %u of %u", (unsigned)(k + 1), (unsigned)days);
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
        if (!ask("note.add " SD_FILE, req)) {
            screen_line(2, SCREEN_RED, "note.add failed, see serial");
        }
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

// One number from the last answer, or -1.
static double num(const char *path)
{
    double v;
    return nc_num(rsp, path, &v) ? v : -1;
}

static void show_signal(void)
{
    if (!ask_name("card.wireless")) {
        screen_line(3, SCREEN_WHITE, "signal: no answer");
        return;
    }
    char rat[16] = "";
    nc_str(rsp, "net.rat", rat, sizeof rat);
    double bars = num("net.bars");
    if (bars < 0) {
        screen_line(3, SCREEN_WHITE, "signal: none yet %s", TRANSPORT);
    } else {
        screen_line(3, SCREEN_WHITE, "bars %d %s", (int)bars, rat[0] ? rat : TRANSPORT);
    }
}

static void sync_and_wait(int round)
{
    double notes0 = -1, bytes0 = -1;
    if (ask_name("card.usage.get")) {
        notes0 = num("notes_sent");
        bytes0 = num("bytes_sent");
    }
    int64_t t0 = esp_timer_get_time();
    ask_name("hub.sync");
    bool ended = false;
    long elapsed = 0;
    // Ended once the answer reports a sync completed since this one was asked
    // ("completed": seconds since the last sync ended, no "sync":true pending),
    // or says {sync-end}. The Notecard drops "status" once the modem is off.
    for (int s = 0; s < SYNC_LIMIT_S && !ended; s += 10) {
        elapsed = (long)((esp_timer_get_time() - t0) / 1000000);
        screen_line(1, SCREEN_YELLOW, "SYNC %d %ld:%02ld of %d min", round, elapsed / 60,
                    elapsed % 60, SYNC_LIMIT_S / 60);
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!ask_name("hub.sync.status")) {
            continue;
        }
        char status[96];
        double completed;
        elapsed = (long)((esp_timer_get_time() - t0) / 1000000);
        bool pending = strstr(rsp, "\"sync\":true") != NULL;
        bool has_status = nc_str(rsp, "status", status, sizeof status);
        if (has_status) {
            screen_line(2, SCREEN_WHITE, "%.33s", status);
        }
        ended = (has_status && strstr(status, "{sync-end}") != NULL) ||
                (!pending && nc_num(rsp, "completed", &completed) && completed <= elapsed);
    }
    elapsed = (long)((esp_timer_get_time() - t0) / 1000000);
    printf("satprobe: sync %s after_s=%ld %s\n", ended ? "ended" : "did not end", elapsed,
           ended ? "ok" : "CHECK");
    if (ended) {
        screen_line(1, SCREEN_GREEN, "DONE sync %d in %ld:%02ld", round, elapsed / 60,
                    elapsed % 60);
    } else {
        screen_line(1, SCREEN_RED, "NO SYNC %d after %ld min", round, elapsed / 60);
    }
    if (ask_name("card.usage.get") && notes0 >= 0 && bytes0 >= 0) {
        screen_line(2, SCREEN_WHITE, "notes +%d bytes +%d", (int)(num("notes_sent") - notes0),
                    (int)(num("bytes_sent") - bytes0));
    } else {
        screen_line(2, SCREEN_WHITE, "usage: see serial");
    }
    printf("satprobe: now check Notehub events (day.qo), the route log and ThingsBoard\n");
}

static void keys_start(void)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << TDISPLAY_S3_AMOLED_KEY |
                                       1ULL << TDISPLAY_S3_AMOLED_KEY_BOOT,
                       .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&c);
}

static bool key_down(void)
{
    return gpio_get_level(TDISPLAY_S3_AMOLED_KEY) == 0 ||
           gpio_get_level(TDISPLAY_S3_AMOLED_KEY_BOOT) == 0;
}

// Shows the signal every 60 s until a key is pressed and released (pressed for
// at least 100 ms); with notecard false only waits.
static void wait_key(bool notecard)
{
    int64_t next = 0;
    int held = 0;
    for (;;) {
        if (notecard && esp_timer_get_time() >= next) {
            show_signal();
            next = esp_timer_get_time() + 60 * 1000000LL;
        }
        held = key_down() ? held + 1 : 0;
        if (held >= 2) {
            while (key_down()) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            printf("satprobe: key pressed\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
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
    keys_start();
    screen_start();
    screen_line(0, SCREEN_WHITE, "SAT TEST %s%s%s", TRANSPORT, MINMAX ? " minmax" : "",
                OVERSIZE ? " +big" : "");
    bool nc = i2c_master_probe(bus, NOTECARD_I2C_ADDR, 50) == ESP_OK;
    printf("satprobe: notecard=%s\n", nc ? "yes" : "no CHECK");
    notecard_config_t cfg = {.i2c_port = 0, .address = NOTECARD_I2C_ADDR, .clk_hz = 100000,
                             .timeout_s = 10};
    esp_err_t err = notecard_start(&cfg);
    printf("satprobe: start=%s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        screen_line(1, SCREEN_RED, "NO NOTECARD");
        screen_line(2, SCREEN_WHITE, "%s", esp_err_to_name(err));
        screen_line(5, SCREEN_WHITE, "check the Qwiic cable, then reset");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }
    screen_line(1, SCREEN_YELLOW, "SETTING UP the Notecard");
    ask_name("card.version");
    ask_name("card.wireless");
    ask_name("card.time");
    setup_hub();
    templates();
    if (WAIT_KEY) {
        screen_line(1, SCREEN_GREEN, "READY: press a key");
        screen_line(2, SCREEN_WHITE, "to send %u days and sync",
                    (unsigned)(sizeof sp_days / sizeof sp_days[0]));
        screen_line(5, SCREEN_WHITE, "outdoors, clear view of the southern sky");
        wait_key(true);
    }
    screen_line(2, SCREEN_WHITE, "%s", "");
    send_days();
    for (int round = 1;; round++) {
        sync_and_wait(round);
        screen_line(5, SCREEN_WHITE, "check Notehub; a key syncs again");
        wait_key(true);
        screen_line(2, SCREEN_WHITE, "%s", "");
    }
}
