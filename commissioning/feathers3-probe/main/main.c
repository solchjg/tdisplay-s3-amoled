// Commissioning aid, not product code: proves the Unexpected Maker FeatherS3D
// parts that Phase 21 (FSD §40) rests on, before any firmware uses them. On
// power-on, after 5 s for the host to open the USB console, it
//   1. prints the chip, its flash size, MAC (the bench pins SLOT11 to it) and
//      whether the package carries PSRAM, and which kind: octal PSRAM in the
//      package takes GPIO 33 - 37 (R-44), so those pins leave the walk;
//   2. scans the always-on bus (I2C1: the horizontal STEMMA QT, SDA 8, SCL 9,
//      LDO1) and reads the board's MAX17048 at 0x36;
//   3. switches LDO2 on (GPIO 39 high) and scans the switched bus (I2C2: the
//      vertical STEMMA QT, SDA 16, SCL 15), reading the BME280's chip id; then
//      LDO2 off and the same scan, which should find nothing; LDO2 stays off,
//      then on, CONFIG_PROBE_LDO2_HOLD_S each, for a meter on that socket's 3V3;
//   4. watches the user key on GPIO 1 (low when pressed), the blue LED on
//      GPIO 13 following it;
//   5. pin walk (R-44): toggles each header pin §40.2 uses at 1 Hz for
//      CONFIG_PROBE_WALK_S s, one at a time, so a meter or LED finds it;
//   6. sleep current (NFR-40.1): deep sleep CONFIG_PROBE_SLEEP_S with LDO2 off
//      and held off; measure the battery current. USB leaves the host while
//      the chip sleeps and returns when it wakes.
// Lines start with "fs3probe:"; CHECK marks a result to look at. The results
// are repeated in a "fs3probe: recap" block before the sleep and after it.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_I2C1_SDA 8
#define PIN_I2C1_SCL 9
#define PIN_I2C2_SDA 16
#define PIN_I2C2_SCL 15
#define PIN_LDO2 39
#define PIN_KEY 1
#define PIN_LED 13
#define GAUGE_ADDR 0x36
#define BME_ADDR_A 0x76
#define BME_ADDR_B 0x77

// §40.2's header pins (GPIO 8/9 and 15/16 are the buses, GPIO 0 the bench's BOOT).
static const int WALK[] = {1, 5, 6, 7, 10, 11, 12, 14, 17, 18, 33, 35, 36};

static RTC_DATA_ATTR int stage;  // 0 power-on, 1 after the sleep
static RTC_DATA_ATTR char recap[3072];
static RTC_DATA_ATTR size_t recap_n;

static bool octal_psram;

// Prints a "fs3probe: " line and keeps it for the recap.
static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    printf("fs3probe: %s\n", line);
    int w = snprintf(recap + recap_n, sizeof(recap) - recap_n, "fs3probe: recap %s\n", line);
    if (w > 0 && recap_n + w < sizeof(recap)) {
        recap_n += w;
    }
}

static void recap_print(void)
{
    printf("fs3probe: recap begins\n%sfs3probe: recap ends\n", recap);
    fflush(stdout);
}

static void wait_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// --- chip ---------------------------------------------------------------

static void chip(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint8_t cap = 0;
    esp_efuse_read_field_blob(ESP_EFUSE_PSRAM_CAP, &cap, 2);
    bool emb = (info.features & CHIP_FEATURE_EMB_PSRAM) != 0;
    // ESP32-S3 packages: R2 carries 2 MB quad PSRAM, R8 8 MB octal PSRAM, which
    // uses GPIO 33 - 37 for its extra data lines.
    octal_psram = emb && cap == 1;
    say("chip model=%d rev=%d cores=%d flash=%lu MB features=0x%lx", info.model, info.revision,
        info.cores, (unsigned long)(flash >> 20), (unsigned long)info.features);
    say("mac %02X:%02X:%02X:%02X:%02X:%02X CHECK (pin SLOT11 to it)", mac[0], mac[1], mac[2], mac[3],
        mac[4], mac[5]);
    say("psram in package=%s cap=%u (%s)%s", emb ? "yes" : "no", cap,
        cap == 1 ? "8 MB octal" : cap == 2 ? "2 MB quad" : "none in package",
        octal_psram ? " CHECK: GPIO 33 - 37 are taken, R-44" : "; GPIO 33 - 37 free");
}

// --- buses ---------------------------------------------------------------

static i2c_master_bus_handle_t bus_open(int port, int sda, int scl, bool pullup)
{
    i2c_master_bus_config_t cfg = {.i2c_port = port,
                                   .sda_io_num = sda,
                                   .scl_io_num = scl,
                                   .clk_source = I2C_CLK_SRC_DEFAULT,
                                   .glitch_ignore_cnt = 7,
                                   .flags.enable_internal_pullup = pullup};
    i2c_master_bus_handle_t b = NULL;
    esp_err_t err = i2c_new_master_bus(&cfg, &b);
    if (err != ESP_OK) {
        say("bus i2c%d %s CHECK", port + 1, esp_err_to_name(err));
        return NULL;
    }
    return b;
}

// Scans the bus; returns how many answered and prints them.
static int scan(i2c_master_bus_handle_t b, const char *name, bool *seen)
{
    char list[160] = "";
    size_t n = 0;
    int found = 0;
    for (int a = 0x08; a < 0x78; a++) {
        bool hit = i2c_master_probe(b, a, 20) == ESP_OK;
        if (seen) {
            seen[a] = hit;
        }
        if (hit) {
            found++;
            n += snprintf(list + n, sizeof(list) - n, " 0x%02x", a);
            if (n >= sizeof(list)) {
                n = sizeof(list) - 1;
            }
        }
    }
    say("%s found %d:%s", name, found, found ? list : " none");
    return found;
}

static esp_err_t reg_read(i2c_master_bus_handle_t b, uint8_t addr, uint8_t reg, uint8_t *v, size_t n)
{
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                              .device_address = addr,
                              .scl_speed_hz = 100000};
    i2c_master_dev_handle_t d;
    esp_err_t err = i2c_master_bus_add_device(b, &dc, &d);
    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(d, &reg, 1, v, n, 100);
        i2c_master_bus_rm_device(d);
    }
    return err;
}

static void always_on_bus(void)
{
    i2c_master_bus_handle_t b = bus_open(0, PIN_I2C1_SDA, PIN_I2C1_SCL, true);
    if (!b) {
        return;
    }
    bool seen[128] = {0};
    scan(b, "i2c1 (horizontal, LDO1; 0x36 gauge, 0x32 RV-8803, 0x17 Notecard)", seen);
    if (!seen[GAUGE_ADDR]) {
        say("gauge none at 0x36 CHECK");
    } else {
        uint8_t v[2] = {0}, s[2] = {0}, ver[2] = {0};
        esp_err_t e1 = reg_read(b, GAUGE_ADDR, 0x02, v, 2);
        esp_err_t e2 = reg_read(b, GAUGE_ADDR, 0x04, s, 2);
        esp_err_t e3 = reg_read(b, GAUGE_ADDR, 0x08, ver, 2);
        unsigned mv = (unsigned)(((v[0] << 8) | v[1]) * 78125ULL / 1000000ULL);
        say("gauge mv=%u pct=%u.%02u version=0x%02x%02x%s (no LiPo: the reading means little)", mv,
            s[0], s[1] * 100 / 256, ver[0], ver[1],
            (e1 || e2 || e3) ? " CHECK read failed" : "");
    }
    i2c_del_master_bus(b);
}

static void ldo2(bool on)
{
    gpio_hold_dis(PIN_LDO2);
    gpio_config_t c = {.pin_bit_mask = 1ULL << PIN_LDO2, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(PIN_LDO2, on);
}

// The bus pins with no pull, so nothing on the board feeds the unpowered side.
static void bus_pins_float(void)
{
    gpio_config_t c = {.pin_bit_mask = (1ULL << PIN_I2C2_SDA) | (1ULL << PIN_I2C2_SCL),
                       .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_DISABLE,
                       .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&c);
}

static void switched_bus(void)
{
    ldo2(true);
    wait_ms(100);
    i2c_master_bus_handle_t b = bus_open(1, PIN_I2C2_SDA, PIN_I2C2_SCL, true);
    if (b) {
        bool seen[128] = {0};
        int n = scan(b, "i2c2 on (vertical, LDO2; 0x32 RV-8803, 0x48 ADS1115, 0x76/0x77 BME280)",
                     seen);
        if (n == 0) {
            say("i2c2 on: nothing answered CHECK (star plugged in the vertical socket?)");
        }
        for (int a = BME_ADDR_A; a <= BME_ADDR_B; a++) {
            if (seen[a]) {
                uint8_t id = 0;
                esp_err_t e = reg_read(b, a, 0xD0, &id, 1);
                say("bme280 0x%02x chip id=0x%02x%s", a, id,
                    e ? " CHECK read failed" : id == 0x60 ? " (BME280)" : id == 0x58 ? " (BMP280: no humidity) CHECK" : " CHECK");
            }
        }
        i2c_del_master_bus(b);
    }

    // LDO2 off: the socket's devices lose power and should vanish.
    bus_pins_float();
    ldo2(false);
    wait_ms(300);
    b = bus_open(1, PIN_I2C2_SDA, PIN_I2C2_SCL, false);
    if (b) {
        int n = scan(b, "i2c2 off (LDO2 off; expect none)", NULL);
        if (n) {
            say("i2c2 off: devices still answer CHECK (LDO2 not switching, or backfed)");
        }
        i2c_del_master_bus(b);
    }
    bus_pins_float();

    if (CONFIG_PROBE_LDO2_HOLD_S) {
        printf("fs3probe: ldo2 OFF for %d s: the vertical socket's 3V3 should read 0 V now\n",
               CONFIG_PROBE_LDO2_HOLD_S);
        fflush(stdout);
        wait_ms(CONFIG_PROBE_LDO2_HOLD_S * 1000);
        ldo2(true);
        printf("fs3probe: ldo2 ON for %d s: the vertical socket's 3V3 should read 3.3 V now\n",
               CONFIG_PROBE_LDO2_HOLD_S);
        fflush(stdout);
        wait_ms(CONFIG_PROBE_LDO2_HOLD_S * 1000);
    }
    ldo2(false);
}

// --- user key and pin walk ------------------------------------------------

static void key(void)
{
    if (CONFIG_PROBE_KEY_S == 0) {
        return;
    }
    gpio_config_t k = {.pin_bit_mask = 1ULL << PIN_KEY, .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&k);
    gpio_config_t l = {.pin_bit_mask = 1ULL << PIN_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&l);
    printf("fs3probe: key gpio1 idle=%d; press it within %d s (the blue LED follows)\n",
           gpio_get_level(PIN_KEY), CONFIG_PROBE_KEY_S);
    fflush(stdout);
    int presses = 0, last = 1;
    for (int i = 0; i < CONFIG_PROBE_KEY_S * 100; i++) {
        int v = gpio_get_level(PIN_KEY);
        gpio_set_level(PIN_LED, !v);
        if (last && !v) {
            presses++;
        }
        last = v;
        wait_ms(10);
    }
    gpio_set_level(PIN_LED, 0);
    gpio_reset_pin(PIN_KEY);
    say("key gpio1 presses=%d%s", presses, presses ? "" : " (none seen; fine if no key is wired)");
}

static void walk(void)
{
    if (CONFIG_PROBE_WALK_S == 0) {
        return;
    }
    for (size_t i = 0; i < sizeof(WALK) / sizeof(WALK[0]); i++) {
        int p = WALK[i];
        if (octal_psram && p >= 33 && p <= 37) {
            say("walk gpio%d skipped: octal PSRAM CHECK (R-44: move it)", p);
            continue;
        }
        printf("fs3probe: walk gpio%d toggling %d s\n", p, CONFIG_PROBE_WALK_S);
        fflush(stdout);
        gpio_config_t c = {.pin_bit_mask = 1ULL << p, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&c);
        for (int t = 0; t < CONFIG_PROBE_WALK_S * 2; t++) {
            gpio_set_level(p, t & 1);
            wait_ms(500);
        }
        gpio_reset_pin(p);
    }
    say("walk done: %d pins", (int)(sizeof(WALK) / sizeof(WALK[0])));
}

// --- sleep ---------------------------------------------------------------

static void sleep_now(void)
{
    ldo2(false);
    gpio_hold_en(PIN_LDO2);
    gpio_deep_sleep_hold_en();
    printf("fs3probe: sleep %d s with LDO2 off: measure the battery current now\n",
           CONFIG_PROBE_SLEEP_S);
    fflush(stdout);
    wait_ms(200);
    esp_sleep_enable_timer_wakeup(CONFIG_PROBE_SLEEP_S * 1000000ULL);
    esp_deep_sleep_start();
}

void app_main(void)
{
    wait_ms(5000);  // the host opens the USB console
    if (stage == 1) {
        stage = 0;
        gpio_hold_dis(PIN_LDO2);
        printf("fs3probe: woke from the sleep\n");
        recap_print();
        printf("fs3probe: done; reset the board to run again\n");
        return;
    }
    recap_n = 0;
    recap[0] = '\0';
    printf("fs3probe: start (FSD §40, Phase 21)\n");
    chip();
    always_on_bus();
    switched_bus();
    key();
    walk();
    recap_print();
    if (CONFIG_PROBE_SLEEP_S) {
        stage = 1;
        sleep_now();
    }
    printf("fs3probe: done\n");
}
