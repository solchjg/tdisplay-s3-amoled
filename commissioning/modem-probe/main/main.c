// Commissioning aid, not product code: evaluates a plain LTE-M modem as the
// vendor-neutral cellular path (no Notehub between the device and its peers)
// on a LilyGO T-SIM7080G-S3 (ESP32-S3, SIMCom SIM7080G, AXP2101 PMU). It runs
// on that board alone, not on the T-Display. Each run
//   1. sets the AXP2101 rails: DC3 3.0 V (modem), BLDO1 3.3 V (level shifter),
//      BLDO2 3.3 V (GPS antenna) only while seeking a fix; prints VBAT;
//   2. wakes the modem (PWRKEY pulse when AT gets no answer) and prints its
//      firmware, SIM state, ICCID and signal;
//   3. on the first run, optionally seeks one GPS fix with LTE off (the
//      SIM7080G cannot run GNSS and LTE at once), as FR-33.15 does once per
//      setup check;
//   4. registers on LTE-M (or NB-IoT), timing the attach, optionally asking
//      for PSM, and prints the operator, band and granted PSM timers;
//   5. brings up PPP through esp_modem, so the stack has an ordinary IP link,
//      sets the clock by SNTP and POSTs one §32.3-shaped telemetry record
//      straight to ThingsBoard, and on the first run one alert through the
//      shared ntfy_tx, the same sender the WiFi path uses;
//   6. leaves PPP and prints the run's timings and the running totals.
// An optional SSD1306 on GPIO 13/21 shows the run's progress and results.
// With "Minutes between runs" 0 it then prints the signal every 30 s; above 0
// it deep-sleeps until the next run (the 24 h soak), the modem in PSM or
// powered down. Lines start with "modemprobe:"; CHECK marks a result to look at.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_netif_sntp.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "ntfy_tx.h"
#include "oled.h"
#include "sdkconfig.h"

// LilyGO T-SIM7080G-S3 (utilities.h of LilyGo-T-SIM7080G, examples/ATDebug).
#define PIN_PMU_SDA 15
#define PIN_PMU_SCL 7
#define PIN_MODEM_PWRKEY 41
#define PIN_MODEM_DTR 42
#define PIN_MODEM_RX 4  // ESP32-S3 receives (modem TXD)
#define PIN_MODEM_TX 5  // ESP32-S3 sends (modem RXD)

// AXP2101 (XPowersLib AXP2101Constants.h).
#define AXP_ADDR 0x34
#define AXP_CHIP_ID 0x4A
#define AXP_REG_ID 0x03
#define AXP_REG_ADC_CTRL 0x30
#define AXP_REG_VBAT_H 0x34
#define AXP_REG_DC_ONOFF 0x80
#define AXP_REG_DC3_VOL 0x84
#define AXP_REG_LDO_ONOFF0 0x90
#define AXP_REG_BLDO1_VOL 0x96
#define AXP_REG_BLDO2_VOL 0x97
#define AXP_DC3_3V0 (88 + (3000 - 1600) / 100)  // 100 mV steps from 1.6 V at code 88
#define AXP_BLDO_3V3 ((3300 - 500) / 100)       // 100 mV steps from 0.5 V
#define AXP_BIT_DC3 2
#define AXP_BIT_BLDO1 4
#define AXP_BIT_BLDO2 5

#define AT_TIMEOUT_MS 1000
#define GPS_ASK_S 5
#define PPP_LIMIT_MS 60000
#define HTTP_TIMEOUT_MS 20000

#if defined(CONFIG_PROBE_RAT_NB)
#define RAT_CMD "AT+CMNB=2"
#elif defined(CONFIG_PROBE_RAT_BOTH)
#define RAT_CMD "AT+CMNB=3"
#else
#define RAT_CMD "AT+CMNB=1"
#endif
#ifdef CONFIG_PROBE_GPS
#define PROBE_GPS 1
#define GPS_LIMIT_S CONFIG_PROBE_GPS_LIMIT_S
#else
#define PROBE_GPS 0
#define GPS_LIMIT_S 0
#endif
#ifdef CONFIG_PROBE_PSM
#define PROBE_PSM 1
#else
#define PROBE_PSM 0
#endif

#define GOT_IP BIT0

// Running totals over the soak, kept through deep sleep.
static RTC_DATA_ATTR int runs, attach_ok, tb_tries, tb_ok, ntfy_tries, ntfy_ok;
static RTC_DATA_ATTR int64_t attach_ms_sum;
// The run-1 GPS result, kept so every later run shows and sends it too.
// gps_s: -1 not tried, -2 no fix, else seconds to the fix.
static RTC_DATA_ATTR int gps_s = -1;
static RTC_DATA_ATTR double gps_lat, gps_lon;

static void title_line(void)
{
    char g[12];
    if (gps_s >= 0) {
        snprintf(g, sizeof g, "%ds", gps_s);
    }
    oled_line(0, "run %d GPS %s", runs, gps_s >= 0 ? g : gps_s == -2 ? "none" : "-");
}

static i2c_master_dev_handle_t pmu;
static char tb_result[12] = "-", ntfy_result[12] = "-";
static esp_modem_dce_t *dce;
static EventGroupHandle_t net_events;
static char out[CONFIG_ESP_MODEM_C_API_STR_MAX];

static long ms_since(int64_t t0_us)
{
    return (long)((esp_timer_get_time() - t0_us) / 1000);
}

// ---- AXP2101 ----

static bool pmu_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(pmu, &reg, 1, val, 1, 100) == ESP_OK;
}

static bool pmu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(pmu, buf, 2, 100) == ESP_OK;
}

static bool pmu_bit(uint8_t reg, int bit, bool on)
{
    uint8_t v;
    if (!pmu_read(reg, &v)) {
        return false;
    }
    v = on ? (uint8_t)(v | (1u << bit)) : (uint8_t)(v & ~(1u << bit));
    return pmu_write(reg, v);
}

static bool pmu_field(uint8_t reg, uint8_t keep_mask, uint8_t code)
{
    uint8_t v;
    return pmu_read(reg, &v) && pmu_write(reg, (uint8_t)((v & keep_mask) | code));
}

static void gps_antenna(bool on)
{
    bool ok = (!on || pmu_field(AXP_REG_BLDO2_VOL, 0xE0, AXP_BLDO_3V3)) &&
              pmu_bit(AXP_REG_LDO_ONOFF0, AXP_BIT_BLDO2, on);
    printf("modemprobe: gps antenna (BLDO2) %s %s\n", on ? "on" : "off", ok ? "ok" : "CHECK");
}

static unsigned vbat_mv(void)
{
    uint8_t h, l;
    if (!pmu_read(AXP_REG_VBAT_H, &h) || !pmu_read(AXP_REG_VBAT_H + 1, &l)) {
        return 0;
    }
    return ((unsigned)(h & 0x1F) << 8) | l;
}

static bool pmu_start(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bc = {
        .i2c_port = 0,
        .sda_io_num = PIN_PMU_SDA,
        .scl_io_num = PIN_PMU_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP_ADDR,
        .scl_speed_hz = 400000,
    };
    uint8_t id = 0;
    if (i2c_new_master_bus(&bc, &bus) != ESP_OK ||
        i2c_master_bus_add_device(bus, &dc, &pmu) != ESP_OK || !pmu_read(AXP_REG_ID, &id)) {
        printf("modemprobe: pmu no answer at 0x%02x CHECK\n", AXP_ADDR);
        return false;
    }
    // BLDO1 feeds the level shifter between the ESP32-S3 and the modem: never off.
    bool ok = pmu_field(AXP_REG_BLDO1_VOL, 0xE0, AXP_BLDO_3V3) &&
              pmu_bit(AXP_REG_LDO_ONOFF0, AXP_BIT_BLDO1, true) &&
              pmu_field(AXP_REG_DC3_VOL, 0x80, AXP_DC3_3V0) &&
              pmu_bit(AXP_REG_DC_ONOFF, AXP_BIT_DC3, true) &&
              pmu_bit(AXP_REG_ADC_CTRL, 0, true);
    printf("modemprobe: pmu id=0x%02x%s rails dc3=3.0V bldo1=3.3V %s\n", id,
           id == AXP_CHIP_ID ? "" : " (not AXP2101) CHECK", ok ? "ok" : "CHECK");
    if (!PROBE_GPS || runs > 1) {
        gps_antenna(false);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    printf("modemprobe: vbat_mv=%u\n", vbat_mv());
    return ok;
}

// ---- modem ----

// Sends one AT command and prints the whole answer; true once pass came back.
static bool at_until(const char *cmd, const char *pass, int timeout_ms)
{
    char line[96];
    snprintf(line, sizeof line, "%s\r", cmd);
    int64_t t0 = esp_timer_get_time();
    out[0] = '\0';
    esp_err_t err = esp_modem_at_raw(dce, line, out, pass, "ERROR", timeout_ms);
    for (char *p = out; *p; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = ' ';
        }
    }
    printf("modemprobe: %s ms=%ld %s%s\n", cmd, ms_since(t0), err == ESP_OK ? "" : "FAILED ",
           out);
    return err == ESP_OK;
}

static bool at(const char *cmd, int timeout_ms)
{
    return at_until(cmd, "OK", timeout_ms);
}

static bool modem_answers(int tries)
{
    for (int i = 0; i < tries; i++) {
        if (esp_modem_sync(dce) == ESP_OK) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

// PWRKEY as LilyGO drives it: low, high for 1 s, low (the modem toggles on,
// or wakes from PSM).
static void pwrkey_pulse(void)
{
    gpio_hold_dis(PIN_MODEM_PWRKEY);
    gpio_set_level(PIN_MODEM_PWRKEY, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_MODEM_PWRKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(PIN_MODEM_PWRKEY, 0);
}

static bool modem_wake(void)
{
    int64_t t0 = esp_timer_get_time();
    bool pulsed = false;
    if (!modem_answers(3)) {
        pwrkey_pulse();
        pulsed = true;
        if (!modem_answers(30)) {
            printf("modemprobe: modem no answer after PWRKEY ms=%ld CHECK\n", ms_since(t0));
            return false;
        }
    }
    printf("modemprobe: modem answers pwrkey=%s ms=%ld\n", pulsed ? "pulsed" : "no", ms_since(t0));
    return true;
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_PPP_GOT_IP) {
        ip_event_got_ip_t *e = data;
        printf("modemprobe: ppp ip=" IPSTR "\n", IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(net_events, GOT_IP);
    } else if (id == IP_EVENT_PPP_LOST_IP) {
        printf("modemprobe: ppp lost ip\n");
        xEventGroupClearBits(net_events, GOT_IP);
    }
}

static bool modem_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    net_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip, NULL));
    esp_netif_config_t nc = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *netif = esp_netif_new(&nc);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_MODEM_PWRKEY) | (1ULL << PIN_MODEM_DTR),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_hold_dis(PIN_MODEM_PWRKEY);
    gpio_config(&io);
    gpio_set_level(PIN_MODEM_PWRKEY, 0);
    gpio_set_level(PIN_MODEM_DTR, 0);  // DTR low keeps the UART awake

    esp_modem_dte_config_t dte = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte.uart_config.port_num = UART_NUM_1;
    dte.uart_config.tx_io_num = PIN_MODEM_TX;
    dte.uart_config.rx_io_num = PIN_MODEM_RX;
    dte.uart_config.rts_io_num = -1;
    dte.uart_config.cts_io_num = -1;
    dte.uart_config.baud_rate = 115200;
    esp_modem_dce_config_t dc = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_PROBE_APN);
    dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7070, &dte, &dc, netif);
    if (dce == NULL) {
        printf("modemprobe: esp_modem_new_dev failed CHECK\n");
        return false;
    }
    return modem_wake();
}

static void modem_info(void)
{
    at("AT+CGMR", AT_TIMEOUT_MS);
    at("AT+CPIN?", AT_TIMEOUT_MS);
    at("AT+CCID", AT_TIMEOUT_MS);
    at("AT+CSQ", AT_TIMEOUT_MS);
}

// "+CEREG: <n>,<stat>[,...]": registered at home (1) or roaming (5).
static bool registered(void)
{
    if (!at("AT+CEREG?", AT_TIMEOUT_MS)) {
        return false;
    }
    const char *p = strstr(out, "+CEREG:");
    int n, stat;
    return p != NULL && sscanf(p, "+CEREG: %d,%d", &n, &stat) == 2 && (stat == 1 || stat == 5);
}

// The CSQ signal value (0 - 31, 99 unknown) from a fresh AT+CSQ.
static int csq_now(void)
{
    int rssi = 99, ber;
    const char *p = at("AT+CSQ", AT_TIMEOUT_MS) ? strstr(out, "+CSQ:") : NULL;
    if (p != NULL) {
        sscanf(p, "+CSQ: %d,%d", &rssi, &ber);
    }
    return rssi;
}

// "Verizon M1 B13" from AT+COPS? ("<first word of the operator>") and AT+CPSI?
// ("LTE CAT-M1,...,EUTRAN-BAND13,...").
static void network_line(void)
{
    char op[16] = "?", band[8] = "?";
    const char *rat = "?";
    const char *p = at("AT+COPS?", AT_TIMEOUT_MS) ? strchr(out, '"') : NULL;
    if (p != NULL) {
        sscanf(p + 1, "%15[^ \"]", op);
    }
    if (at("AT+CPSI?", AT_TIMEOUT_MS)) {
        rat = strstr(out, "CAT-M1") ? "M1" : strstr(out, "NB-IOT") ? "NB" : "LTE";
        p = strstr(out, "BAND");
        if (p != NULL) {
            sscanf(p + 4, "%7[0-9]", band);
        }
    }
    oled_line(1, "%s %s B%s", op, rat, band);
}

// GNSS and LTE share the SIM7080G's RF: radio off, GNSS on, ask every 5 s.
// When the modem stops answering, it is woken and GNSS switched on again, at
// most GPS_RECOVERIES times, rather than asked in vain until the limit.
#define GPS_RECOVERIES 3
static void gps_fix(void)
{
    at("AT+CFUN=0", 10000);
    gps_antenna(true);
    at("AT+CGNSPWR=1", AT_TIMEOUT_MS);
    int64_t t0 = esp_timer_get_time();
    int misses = 0, recoveries = 0;
    gps_s = -2;
    oled_line(1, "GPS: seeking fix");
    while (ms_since(t0) < GPS_LIMIT_S * 1000L && gps_s < 0) {
        vTaskDelay(pdMS_TO_TICKS(GPS_ASK_S * 1000));
        oled_line(2, "GPS %lds", ms_since(t0) / 1000);
        if (!at("AT+CGNSINF", 2000)) {
            if (++misses < 3) {
                continue;
            }
            if (recoveries++ == GPS_RECOVERIES) {
                printf("modemprobe: gps modem silent, search abandoned CHECK\n");
                break;
            }
            printf("modemprobe: gps modem silent, recovery %d\n", recoveries);
            if (modem_wake()) {
                at("AT+CFUN=0", 10000);
                at("AT+CGNSPWR=1", AT_TIMEOUT_MS);
            }
            misses = 0;
            continue;
        }
        misses = 0;
        // "+CGNSINF: <run>,<fix>,<utc>,<lat>,<lon>,..."
        const char *p = strstr(out, "+CGNSINF:");
        int run, fix;
        char utc[24];
        double lat, lon;
        if (p != NULL && sscanf(p, "+CGNSINF: %d,%d,%23[^,],%lf,%lf", &run, &fix, utc, &lat, &lon) == 5 &&
            fix == 1) {
            gps_s = (int)(ms_since(t0) / 1000);
            gps_lat = lat;
            gps_lon = lon;
            printf("modemprobe: gps fix lat=%.5f lon=%.5f after_s=%d\n", lat, lon, gps_s);
            oled_line(2, "%.4f %.4f", lat, lon);
        }
    }
    if (gps_s < 0) {
        printf("modemprobe: gps no fix within %d s CHECK\n", GPS_LIMIT_S);
        oled_line(2, "GPS no fix");
    }
    title_line();
    at("AT+CGNSPWR=0", AT_TIMEOUT_MS);
    gps_antenna(false);
}

static long attach(void)
{
    char cmd[96];
    at("AT+CFUN=0", 10000);
    at("AT+CNMP=38", AT_TIMEOUT_MS);  // LTE only
    at(RAT_CMD, AT_TIMEOUT_MS);
    snprintf(cmd, sizeof cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", CONFIG_PROBE_APN);
    at(cmd, AT_TIMEOUT_MS);
    at("AT+CEREG=4", AT_TIMEOUT_MS);  // report the granted PSM timers too
    int64_t t0 = esp_timer_get_time();
    at("AT+CFUN=1", 10000);
    // The modem answers CPSMS with ERROR until the SIM is up again after
    // CFUN=1, so retry for a few seconds.
    // TAU 1 h (unit 1 h, value 1), active time 10 s (unit 2 s, value 5).
    for (int i = 0; i < 10; i++) {
        if (at(PROBE_PSM ? "AT+CPSMS=1,,,\"00100001\",\"00000101\"" : "AT+CPSMS=0",
               AT_TIMEOUT_MS)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    oled_line(1, "registering...");
    while (ms_since(t0) < CONFIG_PROBE_ATTACH_S * 1000L) {
        if (registered()) {
            long ms = ms_since(t0);
            printf("modemprobe: registered attach_ms=%ld %s\n", ms, ms <= 60000 ? "ok" : "CHECK");
            network_line();
            oled_line(2, "attach %.1fs CSQ %d", ms / 1000.0, csq_now());
            return ms;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    printf("modemprobe: not registered within %d s CHECK\n", CONFIG_PROBE_ATTACH_S);
    oled_line(1, "NOT registered");
    oled_line(2, "CSQ %d", csq_now());
    at("AT+CPSI?", AT_TIMEOUT_MS);
    return -1;
}

// ---- senders over PPP ----

static void set_clock(void)
{
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sc);
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    printf("modemprobe: sntp %s\n", err == ESP_OK ? "ok" : "failed CHECK");
    esp_netif_sntp_deinit();
}

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec > 1700000000 ? (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 : 0;
}

// One record in the §32.3 shape; the values are fixed test values, plus the
// run's own figures as probe_* keys.
static void post_telemetry(long attach_ms, int rssi)
{
    if (CONFIG_PROBE_TB_URL[0] == '\0' || CONFIG_PROBE_TB_TOKEN[0] == '\0') {
        printf("modemprobe: no ThingsBoard URL or token: telemetry skipped\n");
        return;
    }
    char url[256], vals[384], body[512], gps[96] = "";
    snprintf(url, sizeof url, "%s/api/v1/%s/telemetry", CONFIG_PROBE_TB_URL, CONFIG_PROBE_TB_TOKEN);
    int64_t ts = now_ms();
    if (gps_s >= 0) {
        snprintf(gps, sizeof gps, ",\"probe_gps_s\":%d,\"latitude\":%.6f,\"longitude\":%.6f", gps_s,
                 gps_lat, gps_lon);
    } else if (gps_s == -2) {
        snprintf(gps, sizeof gps, ",\"probe_gps_s\":-1");
    }
    snprintf(vals, sizeof vals,
             "{\"temp_f\":70.00,\"rh_pct\":50.0,\"p_mbar\":1013.25,\"synced\":%s,"
             "\"probe_run\":%d,\"probe_attach_ms\":%ld,\"probe_rssi\":%d,\"probe_vbat_mv\":%u%s}",
             ts ? "true" : "false", runs, attach_ms, rssi, vbat_mv(), gps);
    // With a clock the record carries its own time, as in §32.3; without one
    // ThingsBoard stamps it on arrival.
    int n = ts ? snprintf(body, sizeof body, "[{\"ts\":%" PRId64 ",\"values\":%s}]", ts, vals)
               : snprintf(body, sizeof body, "{\"values\":%s}", vals);
    if (n <= 0 || n >= (int)sizeof body) {
        printf("modemprobe: telemetry body too long CHECK\n");
        return;
    }
    esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = strncmp(url, "https://", 8) == 0 ? esp_crt_bundle_attach : NULL,
    };
    tb_tries++;
    int64_t t0 = esp_timer_get_time();
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, body, n);
    esp_err_t err = esp_http_client_perform(c);
    int status = err == ESP_OK ? esp_http_client_get_status_code(c) : 0;
    esp_http_client_cleanup(c);
    bool ok = status >= 200 && status < 300;
    tb_ok += ok;
    snprintf(tb_result, sizeof tb_result, "%d", status);
    oled_line(3, "TB %s  ntfy %s", tb_result, ntfy_result);
    printf("modemprobe: thingsboard status=%d err=%s bytes=%d ms=%ld %s\n", status,
           esp_err_to_name(err), n, ms_since(t0), ok ? "ok" : "CHECK");
}

static void send_alert(long attach_ms, int rssi)
{
    if (CONFIG_PROBE_NTFY_URL[0] == '\0' || CONFIG_PROBE_NTFY_TOPIC[0] == '\0') {
        printf("modemprobe: no ntfy URL or topic: alert skipped\n");
        return;
    }
    char text[160];
    snprintf(text, sizeof text, "Sent over LTE through the SIM7080G, no Notehub. Attach %ld ms, "
             "signal %d (CSQ), battery %u mV.", attach_ms, rssi, vbat_mv());
    ntfy_tx_server_t server = {
        .url = CONFIG_PROBE_NTFY_URL,
        .topic = CONFIG_PROBE_NTFY_TOPIC,
        .token = CONFIG_PROBE_NTFY_TOKEN,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    nt_msg_t msg = {.title = "modem-probe", .body = text, .priority = 3, .tags = "satellite"};
    nt_reason_t why = NT_OK;
    ntfy_tries++;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = ntfy_tx_send(&server, &msg, &why);
    ntfy_ok += err == ESP_OK;
    snprintf(ntfy_result, sizeof ntfy_result, "%s", nt_reason_name(why));
    oled_line(3, "TB %s  ntfy %s", tb_result, ntfy_result);
    printf("modemprobe: ntfy %s reason=%s ms=%ld %s\n", esp_err_to_name(err), nt_reason_name(why),
           ms_since(t0), err == ESP_OK ? "ok" : "CHECK");
}

static void session(long attach_ms)
{
    int rssi = 99, ber = 99;
    esp_modem_get_signal_quality(dce, &rssi, &ber);
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    EventBits_t bits = err == ESP_OK ? xEventGroupWaitBits(net_events, GOT_IP, pdFALSE, pdTRUE,
                                                           pdMS_TO_TICKS(PPP_LIMIT_MS))
                                     : 0;
    if (!(bits & GOT_IP)) {
        printf("modemprobe: ppp no ip err=%s ms=%ld CHECK\n", esp_err_to_name(err), ms_since(t0));
        oled_line(3, "PPP: no IP");
    } else {
        printf("modemprobe: ppp up ms=%ld\n", ms_since(t0));
        oled_line(3, "PPP up, sending");
        set_clock();
        post_telemetry(attach_ms, rssi);
        if (runs == 1) {
            send_alert(attach_ms, rssi);
        }
    }
    esp_modem_set_mode(dce, ESP_MODEM_MODE_COMMAND);
    printf("modemprobe: ppp session ms=%ld\n", ms_since(t0));
}

static void report(int64_t t_boot)
{
    printf("modemprobe: run=%d awake_ms=%ld attach_ok=%d/%d attach_mean_ms=%" PRId64
           " thingsboard_ok=%d/%d ntfy_ok=%d/%d vbat_mv=%u\n",
           runs, ms_since(t_boot), attach_ok, runs, attach_ok ? attach_ms_sum / attach_ok : 0,
           tb_ok, tb_tries, ntfy_ok, ntfy_tries, vbat_mv());
    oled_line(4, "awake %lds %u.%02uV", ms_since(t_boot) / 1000, vbat_mv() / 1000,
              vbat_mv() % 1000 / 10);
}

static void sleep_until_next(int64_t t_boot)
{
    if (PROBE_PSM) {
        at("AT+CEREG?", AT_TIMEOUT_MS);  // the granted active time and TAU
    } else {
        at_until("AT+CPOWD=1", "POWER DOWN", 5000);  // cold attach next run
    }
    int64_t wait_us = (int64_t)CONFIG_PROBE_EVERY_MIN * 60 * 1000000 -
                      (esp_timer_get_time() - t_boot);
    if (wait_us < 10 * 1000000) {
        wait_us = 10 * 1000000;
    }
    printf("modemprobe: deep sleep s=%" PRId64 "\n", wait_us / 1000000);
    oled_off();
    gpio_set_level(PIN_MODEM_PWRKEY, 0);
    gpio_hold_en(PIN_MODEM_PWRKEY);  // keep PWRKEY released through deep sleep
    esp_sleep_enable_timer_wakeup((uint64_t)wait_us);
    esp_deep_sleep_start();
}

void app_main(void)
{
    int64_t t_boot = esp_timer_get_time();
    runs++;
    printf("modemprobe: run %d wake_causes=0x%" PRIx32 " apn=%s\n", runs,
           esp_sleep_get_wakeup_causes(), CONFIG_PROBE_APN);
    pmu_start();
    oled_start(CONFIG_PROBE_OLED_SDA, CONFIG_PROBE_OLED_SCL);
    title_line();
    oled_line(1, "modem starting");
    bool up = modem_start();
    if (!up) {
        oled_line(1, "modem: no answer");
    }
    if (up) {
        modem_info();
        if (PROBE_GPS && runs == 1) {
            gps_fix();
        }
        long attach_ms = attach();
        if (attach_ms >= 0) {
            attach_ok++;
            attach_ms_sum += attach_ms;
            session(attach_ms);
        }
    }
    report(t_boot);
    if (CONFIG_PROBE_EVERY_MIN > 0) {
        sleep_until_next(t_boot);
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        if (up) {
            oled_line(2, "CSQ %d", csq_now());
        }
        printf("modemprobe: vbat_mv=%u\n", vbat_mv());
    }
}
