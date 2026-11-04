// Commissioning aid, not product code: proves the Blues Notecard of FSD §33
// on the board's bus before the Phase 15 firmware relies on it. Once after
// reset it
//   1. scans the main bus (GPIO 43/44): the Notecard (0x17) and the BME280
//      (0x76) must answer, the Notecarrier-X's charger (TI BQ25628E, 0x6A)
//      with the carrier fitted, the water ADS1115 (0x48) when its board is;
//   2. reads card.version, card.transport, hub.get, card.wireless (signal
//      bars) and card.time, printing each answer;
//   3. with a ProductUID set (menuconfig, "Notecard probe"), sends hub.set and
//      card.transport cell where the Notecard differs (FR-33.4, FR-33.14);
//   4. adds one note of 100 readings shaped as a §32.3 telemetry batch, and
//      one of 50, to probe.qo (no Notehub route takes that file) and prints
//      each note's size and how long note.add took (FR-33.7: within 10 s);
//   5. optionally forces one sync and prints its data use from card.usage.get
//      before and after (NFR-33.2);
//   6. optionally seeks a GPS fix, asking card.location every 5 s for up to
//      20 min, then sets the location mode off again (FR-33.15, R-35).
// Then it prints the signal bars every 30 s.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "notecard.h"
#include "sdkconfig.h"

#define PIN_SDA 43
#define PIN_SCL 44
#define ADDR_BME280 0x76
#define ADDR_WATER 0x48
#define ADDR_CHARGER 0x6A  // the Notecarrier-X's TI BQ25628E, fixed
#define CELL_EVERY_MIN 60
#define INBOUND_MIN 1440
#define GPS_ASK_S 5
#define GPS_LIMIT_S (20 * 60)
#define SYNC_LIMIT_S 300

#ifdef CONFIG_PROBE_SYNC
#define PROBE_SYNC 1
#else
#define PROBE_SYNC 0
#endif
#ifdef CONFIG_PROBE_GPS
#define PROBE_GPS 1
#else
#define PROBE_GPS 0
#endif

static i2c_master_bus_handle_t bus;
static char req[16384];
static char rsp[1024];

static bool present(uint8_t addr)
{
    return i2c_master_probe(bus, addr, 50) == ESP_OK;
}

static void scan(void)
{
    printf("notecardprobe: scan:");
    int n = 0;
    for (int a = 0x08; a < 0x78; a++) {
        if (present(a)) {
            printf(" 0x%02x", a);
            n++;
        }
    }
    printf("%s\n", n ? "" : " none");
    bool nc = present(NOTECARD_I2C_ADDR), bme = present(ADDR_BME280);
    printf("notecardprobe: notecard=%s bme280=%s charger=%s water=%s %s\n", nc ? "yes" : "no",
           bme ? "yes" : "no", present(ADDR_CHARGER) ? "yes" : "no",
           present(ADDR_WATER) ? "yes" : "no", nc && bme ? "ok" : "CHECK");
}

// Sends one request line, prints the answer and how long it took; true when
// an answer without "err" came back.
static bool ask(const char *what, const char *line)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = notecard_request(line, rsp, sizeof rsp);
    long ms = (long)((esp_timer_get_time() - t0) / 1000);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        printf("notecardprobe: %s failed=%s ms=%ld\n", what, esp_err_to_name(err), ms);
        return false;
    }
    printf("notecardprobe: %s ms=%ld %s\n", what, ms, rsp);
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
        printf("notecardprobe: no ProductUID set: hub.set and card.transport left alone\n");
        return;
    }
    if (!nc_product_valid(product)) {
        printf("notecardprobe: ProductUID '%s' invalid (FSD 33.3) CHECK\n", product);
        return;
    }
    if (ask_name("hub.get") && nc_hub_matches(rsp, product, CELL_EVERY_MIN, INBOUND_MIN)) {
        printf("notecardprobe: hub.set not needed\n");
    } else if (nc_req_hub_set(line, sizeof line, product, CELL_EVERY_MIN, INBOUND_MIN)) {
        ask("hub.set", line);
    }
    if (ask_name("card.transport") && nc_transport_is(rsp, "cell")) {
        printf("notecardprobe: transport already cell\n");
    } else if (nc_req_transport(line, sizeof line, "cell")) {
        ask("card.transport cell", line);
    }
}

// A §32.3 telemetry batch of n readings, ten minutes apart, as body.t.
static size_t batch_body(char *buf, size_t size, int n)
{
    size_t o = (size_t)snprintf(buf, size, "{\"t\":[");
    int64_t ts = 1798761600000LL;  // 2027-01-01T00:00:00Z
    for (int i = 0; i < n && o < size; i++) {
        o += (size_t)snprintf(buf + o, size - o,
                              "%s{\"ts\":%" PRId64 ",\"values\":{\"temp_f\":%d.%02d,"
                              "\"rh_pct\":%d.%d,\"p_mbar\":%d.%02d,\"synced\":true}}",
                              i ? "," : "", ts + (int64_t)i * 600000, 60 + i % 30, i % 100,
                              40 + i % 50, i % 10, 1000 + i % 30, (i * 7) % 100);
    }
    if (o + 3 > size) {
        return 0;
    }
    o += (size_t)snprintf(buf + o, size - o, "]}");
    return o;
}

static void timed_batches(void)
{
    static char body[15000];
    const int sizes[] = {100, 50};
    for (int k = 0; k < 2; k++) {
        size_t blen = batch_body(body, sizeof body, sizes[k]);
        size_t len = blen ? nc_req_note_add(req, sizeof req, "probe.qo", body, false) : 0;
        if (len == 0) {
            printf("notecardprobe: batch %d does not fit CHECK\n", sizes[k]);
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_err_t err = notecard_request(req, rsp, sizeof rsp);
        long ms = (long)((esp_timer_get_time() - t0) / 1000);
        bool ok = err == ESP_OK && !nc_err(rsp, NULL, 0);
        printf("notecardprobe: batch readings=%d body_bytes=%u request_bytes=%u ms=%ld "
               "answer=%s %s\n",
               sizes[k], (unsigned)blen, (unsigned)len, ms,
               err == ESP_OK ? rsp : esp_err_to_name(err), ok && ms <= 10000 ? "ok" : "CHECK");
    }
}

static void forced_sync(void)
{
    if (!PROBE_SYNC) {
        return;
    }
    ask_name("card.usage.get");
    ask_name("hub.sync");
    for (int s = 0; s < SYNC_LIMIT_S; s += 10) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (ask_name("hub.sync.status")) {
            char status[96];
            if (nc_str(rsp, "status", status, sizeof status) &&
                strstr(status, "{sync-end}") != NULL) {
                break;
            }
        }
    }
    ask_name("card.usage.get");
    printf("notecardprobe: data use of the sync = the difference of bytes_sent and "
           "bytes_received above (updated at the end of the session)\n");
}

static void gps_fix(void)
{
    if (!PROBE_GPS) {
        return;
    }
    char line[128];
    if (!nc_req_location_mode(line, sizeof line, "continuous", NULL, NULL) ||
        !ask("card.location.mode continuous", line)) {
        return;
    }
    int64_t t0 = esp_timer_get_time();
    for (int s = 0; s <= GPS_LIMIT_S; s += GPS_ASK_S) {
        double lat, lon;
        if (ask_name("card.location") && nc_num(rsp, "lat", &lat) && nc_num(rsp, "lon", &lon)) {
            printf("notecardprobe: gps fix lat=%.5f lon=%.5f after_s=%lld\n", lat, lon,
                   (long long)((esp_timer_get_time() - t0) / 1000000));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(GPS_ASK_S * 1000));
    }
    if (nc_req_location_mode(line, sizeof line, "off", NULL, NULL)) {
        ask("card.location.mode off", line);
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
    printf("notecardprobe: bus=%s\n", esp_err_to_name(i2c_new_master_bus(&bc, &bus)));
    scan();
    notecard_config_t cfg = {.i2c_port = 0, .address = NOTECARD_I2C_ADDR, .clk_hz = 100000,
                             .timeout_s = 10};
    esp_err_t err = notecard_start(&cfg);
    printf("notecardprobe: start=%s\n", esp_err_to_name(err));
    if (err == ESP_OK) {
        ask_name("card.version");
        ask_name("card.transport");
        ask_name("hub.get");
        ask_name("card.wireless");
        ask_name("card.time");
        setup_hub();
        timed_batches();
        forced_sync();
        gps_fix();
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        if (err == ESP_OK) {
            ask_name("card.wireless");
        }
    }
}
