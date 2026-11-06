// Commissioning aid, not product code: proves the Waveshare
// ESP32-S3-SIM7670G-4G parts that Phase 17 (FSD §35) rests on, before the
// SLOT2 bench runs. On power-on it
//   1. prints the chip, its flash size and MAC (the bench records the MAC);
//   2. scans the sensor bus (I2C 0, GPIO 15/16, §35.2) and reads the
//      MAX17048 (0x36): cell voltage, charge, version;
//   3. microSD (FR-35.4, FR-35.20): the card detect level (GPIO 46) and a
//      1-bit SDMMC mount (CMD 4, CLK 5, D0 6); with a card, lists the root;
//   4. modem (R-36, R-37): whether it answers before GPIO 33 is driven (the
//      4G DIP switch on), then GPIO 33 high and AT on UART 1 over GPIO 17/18,
//      both directions tried; ATI, firmware, SIM, signal, registration time,
//      network, network time; with CONFIG_PROBE_GNSS_S, a GNSS search and
//      whether LTE stays registered during it; then AT+CPOF, and GPIO 33 low;
//   5. off-board key (FR-35.3): GPIO 12 read with no internal pull for 10 s,
//      the LED on GPIO 13 following it;
//   6. pin walk (R-36): toggles each header pin §35.2 uses at 1 Hz for
//      CONFIG_PROBE_WALK_S s, one at a time, so a meter or LED finds it;
//   7. sleep current (NFR-35.3): deep sleep CONFIG_PROBE_SLEEP_S with the
//      modem's supply cut (GPIO 33 low), then the same with the supply on and
//      the modem powered down by AT+CPOF; measure the battery current in each.
// Lines start with "wsprobe:"; CHECK marks a result to look at.

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define PIN_SDA 15
#define PIN_SCL 16
#define GAUGE_ADDR 0x36
#define PIN_SD_CMD 4
#define PIN_SD_CLK 5
#define PIN_SD_D0 6
#define PIN_SD_CD 46
#define PIN_MODEM_PWR 33  // Waveshare FAQ: with the 4G switch off, high powers the modem
#define PIN_MODEM_A 17
#define PIN_MODEM_B 18
#define MODEM_UART UART_NUM_1
#define PIN_KEY 12
#define PIN_LED 13
#define MOUNT "/card"

// §35.2's Pins row: the header GPIOs the build uses (15/16 are the bus).
static const int WALK[] = {7, 8, 9, 10, 11, 12, 13, 14, 39, 40, 41, 42, 45};

static RTC_DATA_ATTR int stage;    // 0 power-on, 1 after the sleep with the supply cut, 2 after both
static RTC_DATA_ATTR int modem_tx; // the ESP's TX pin to the modem once found, 0 unknown

static char rx[1024];

static void wait_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
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
    printf("wsprobe: chip model=%d rev=%d cores=%d flash=%lu MB features=0x%lx\n", info.model,
           info.revision, info.cores, (unsigned long)(flash >> 20), (unsigned long)info.features);
    printf("wsprobe: mac %02x:%02x:%02x:%02x:%02x:%02x CHECK (record for SLOT2)\n", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
}

// --- sensor bus and gauge ------------------------------------------------

static void bus(void)
{
    i2c_master_bus_config_t cfg = {.i2c_port = 0,
                                   .sda_io_num = PIN_SDA,
                                   .scl_io_num = PIN_SCL,
                                   .clk_source = I2C_CLK_SRC_DEFAULT,
                                   .glitch_ignore_cnt = 7,
                                   .flags.enable_internal_pullup = true};
    i2c_master_bus_handle_t b;
    esp_err_t err = i2c_new_master_bus(&cfg, &b);
    if (err != ESP_OK) {
        printf("wsprobe: bus %s CHECK\n", esp_err_to_name(err));
        return;
    }
    printf("wsprobe: bus found:");
    bool gauge = false;
    for (int a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(b, a, 50) == ESP_OK) {
            printf(" 0x%02x", a);
            gauge |= a == GAUGE_ADDR;
        }
    }
    printf(" (0x36 gauge, 0x3c OLED, 0x48 ADS1115, 0x76 BME280)\n");
    if (!gauge) {
        printf("wsprobe: gauge none at 0x36 CHECK\n");
    } else {
        i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                  .device_address = GAUGE_ADDR,
                                  .scl_speed_hz = 100000};
        i2c_master_dev_handle_t d;
        if (i2c_master_bus_add_device(b, &dc, &d) == ESP_OK) {
            uint8_t reg, v[2];
            reg = 0x02;
            esp_err_t e1 = i2c_master_transmit_receive(d, &reg, 1, v, 2, 100);
            unsigned mv = (unsigned)(((v[0] << 8) | v[1]) * 78125ULL / 1000000ULL);
            reg = 0x04;
            uint8_t s[2] = {0};
            esp_err_t e2 = i2c_master_transmit_receive(d, &reg, 1, s, 2, 100);
            reg = 0x08;
            uint8_t ver[2] = {0};
            esp_err_t e3 = i2c_master_transmit_receive(d, &reg, 1, ver, 2, 100);
            printf("wsprobe: gauge mv=%u pct=%u.%02u version=0x%02x%02x %s%s%s\n", mv, s[0],
                   s[1] * 100 / 256, ver[0], ver[1], esp_err_to_name(e1), e2 ? " CHECK" : "",
                   e3 ? " CHECK" : "");
            i2c_master_bus_rm_device(d);
        }
    }
    i2c_del_master_bus(b);
}

// --- microSD -------------------------------------------------------------

static void sd(void)
{
    gpio_config_t cd = {.pin_bit_mask = 1ULL << PIN_SD_CD, .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&cd);
    wait_ms(10);
    printf("wsprobe: sd detect gpio46=%d (note it with and without a card) CHECK\n",
           gpio_get_level(PIN_SD_CD));
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0 = PIN_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 2};
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mc, &card);
    printf("wsprobe: sd mount %s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return;
    }
    printf("wsprobe: sd card %s %llu MB\n", card->cid.name,
           (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024));
    DIR *dir = opendir(MOUNT);
    struct dirent *e;
    while (dir && (e = readdir(dir)) != NULL) {
        printf("wsprobe: sd file %s\n", e->d_name);
    }
    if (dir) {
        closedir(dir);
    }
    esp_vfs_fat_sdcard_unmount(MOUNT, card);
}

// --- modem ---------------------------------------------------------------

static void uart_on(int tx, int rxp)
{
    if (uart_is_driver_installed(MODEM_UART)) {
        uart_driver_delete(MODEM_UART);
    }
    uart_config_t c = {.baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
                       .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
                       .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
    uart_driver_install(MODEM_UART, 2048, 0, 0, NULL, 0);
    uart_param_config(MODEM_UART, &c);
    uart_set_pin(MODEM_UART, tx, rxp, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// Sends cmd and collects the answer until "OK", "ERROR" or `until` (when
// given) appears, or timeout_ms passes. Prints the answer; true on OK/until.
static bool at(const char *cmd, int timeout_ms, const char *until)
{
    uart_flush_input(MODEM_UART);
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);
    size_t n = 0;
    int64_t end = now_ms() + timeout_ms;
    bool ok = false;
    rx[0] = 0;
    while (now_ms() < end && n < sizeof(rx) - 1) {
        int got = uart_read_bytes(MODEM_UART, (uint8_t *)rx + n, sizeof(rx) - 1 - n, pdMS_TO_TICKS(50));
        if (got > 0) {
            n += got;
            rx[n] = 0;
        }
        if ((until && strstr(rx, until)) || strstr(rx, "\r\nOK\r\n")) {
            ok = true;
            break;
        }
        if (strstr(rx, "ERROR")) {
            break;
        }
    }
    for (char *p = rx; *p; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = ' ';
        }
    }
    printf("wsprobe: at %s -> %s%s\n", cmd, n ? rx : "(nothing)", ok ? "" : " [no OK]");
    return ok;
}

// Tries both pin directions for up to wait_s seconds; true when one answers.
static bool find_modem(int wait_s)
{
    int64_t end = now_ms() + wait_s * 1000LL;
    const int dirs[2][2] = {{PIN_MODEM_B, PIN_MODEM_A}, {PIN_MODEM_A, PIN_MODEM_B}};
    while (now_ms() < end) {
        for (int i = 0; i < 2; i++) {
            if (modem_tx && dirs[i][0] != modem_tx) {
                continue;
            }
            uart_on(dirs[i][0], dirs[i][1]);
            for (int k = 0; k < 3; k++) {
                if (at("AT", 300, NULL)) {
                    modem_tx = dirs[i][0];
                    printf("wsprobe: modem answers tx=gpio%d rx=gpio%d\n", dirs[i][0], dirs[i][1]);
                    return true;
                }
            }
        }
        wait_ms(500);
    }
    return false;
}

static void modem_pwr(bool on)
{
    gpio_hold_dis(PIN_MODEM_PWR);
    gpio_config_t c = {.pin_bit_mask = 1ULL << PIN_MODEM_PWR, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(PIN_MODEM_PWR, on);
    gpio_hold_en(PIN_MODEM_PWR);
    printf("wsprobe: modem supply gpio33=%d\n", on);
}

static bool registered(void)
{
    at("AT+CEREG?", 1000, NULL);
    return strstr(rx, ",1") || strstr(rx, ",5");
}

static void gnss(void)
{
    if (CONFIG_PROBE_GNSS_S == 0) {
        return;
    }
    if (!at("AT+CGNSSPWR=1", 30000, "READY")) {
        printf("wsprobe: gnss no READY CHECK\n");
    }
    int64_t t0 = now_ms();
    bool fix = false;
    while (now_ms() - t0 < CONFIG_PROBE_GNSS_S * 1000LL) {
        at("AT+CGNSSINFO", 2000, NULL);
        // An answer with no fix is "+CGNSSINFO: ,,,,,,,,"
        if (strstr(rx, "+CGNSSINFO: ") && !strstr(rx, "+CGNSSINFO: ,")) {
            fix = true;
            break;
        }
        wait_ms(5000);
    }
    printf("wsprobe: gnss fix=%s after %lld s\n", fix ? "yes" : "no CHECK", (now_ms() - t0) / 1000);
    printf("wsprobe: lte during gnss registered=%s\n", registered() ? "yes" : "no");
    at("AT+CGNSSPWR=0", 5000, NULL);
}

static void modem(void)
{
    // GPIO 33 left as input: an answer now means the 4G switch powers it.
    gpio_reset_pin(PIN_MODEM_PWR);
    bool early = find_modem(3);
    printf("wsprobe: modem before gpio33 %s\n", early ? "answers (4G switch on?) CHECK" : "silent");
    modem_pwr(true);
    int64_t t0 = now_ms();
    if (!early && !find_modem(40)) {
        printf("wsprobe: modem no answer within 40 s on either direction CHECK\n");
        return;
    }
    printf("wsprobe: modem answer ms=%lld\n", now_ms() - t0);
    at("ATE0", 1000, NULL);
    at("ATI", 2000, NULL);
    at("AT+CGMR", 2000, NULL);
    at("AT+CPIN?", 5000, NULL);
    at("AT+CICCID", 2000, NULL);
    if (strlen(CONFIG_PROBE_APN) > 0) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", CONFIG_PROBE_APN);
        at(cmd, 2000, NULL);
    }
    int64_t r0 = now_ms();
    bool reg = false;
    while (now_ms() - r0 < CONFIG_PROBE_REGISTER_S * 1000LL) {
        if (registered()) {
            reg = true;
            break;
        }
        wait_ms(2000);
    }
    printf("wsprobe: modem registered=%s ms=%lld (since answer)\n", reg ? "yes" : "no CHECK", now_ms() - r0);
    at("AT+CSQ", 1000, NULL);
    at("AT+COPS?", 5000, NULL);
    at("AT+CPSI?", 2000, NULL);
    at("AT+CCLK?", 1000, NULL);
    gnss();
    int64_t d0 = now_ms();
    at("AT+CPOF", 10000, NULL);
    wait_ms(3000);
    bool still = at("AT", 500, NULL);
    printf("wsprobe: modem after AT+CPOF %s ms=%lld\n", still ? "still answers CHECK" : "silent", now_ms() - d0);
    modem_pwr(false);
}

// --- off-board key and pin walk -----------------------------------------

static void key(void)
{
    gpio_config_t k = {.pin_bit_mask = 1ULL << PIN_KEY, .mode = GPIO_MODE_INPUT};
    gpio_config(&k);
    gpio_config_t l = {.pin_bit_mask = 1ULL << PIN_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&l);
    printf("wsprobe: key gpio12 idle=%d (1 with the 10 kOhm pull-up); press it within 10 s\n",
           gpio_get_level(PIN_KEY));
    int presses = 0, last = 1;
    for (int i = 0; i < 1000; i++) {
        int v = gpio_get_level(PIN_KEY);
        gpio_set_level(PIN_LED, !v);
        if (last && !v) {
            presses++;
        }
        last = v;
        wait_ms(10);
    }
    gpio_set_level(PIN_LED, 0);
    printf("wsprobe: key presses=%d%s\n", presses, presses ? "" : " CHECK (none seen)");
}

static void walk(void)
{
    if (CONFIG_PROBE_WALK_S == 0) {
        return;
    }
    for (size_t i = 0; i < sizeof(WALK) / sizeof(WALK[0]); i++) {
        int p = WALK[i];
        printf("wsprobe: walk gpio%d toggling %d s\n", p, CONFIG_PROBE_WALK_S);
        gpio_config_t c = {.pin_bit_mask = 1ULL << p, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&c);
        for (int t = 0; t < CONFIG_PROBE_WALK_S * 2; t++) {
            gpio_set_level(p, t & 1);
            wait_ms(500);
        }
        gpio_reset_pin(p);
    }
}

// --- sleep ---------------------------------------------------------------

static void sleep_now(const char *what)
{
    printf("wsprobe: sleep %d s with %s: measure the battery current now\n", CONFIG_PROBE_SLEEP_S, what);
    fflush(stdout);
    wait_ms(200);
    esp_sleep_enable_timer_wakeup(CONFIG_PROBE_SLEEP_S * 1000000ULL);
    esp_deep_sleep_start();
}

void app_main(void)
{
    if (stage == 1) {
        printf("wsprobe: woke from the sleep with the modem's supply cut\n");
        modem_pwr(true);
        if (find_modem(40)) {
            at("AT+CPOF", 10000, NULL);
            wait_ms(3000);
        } else {
            printf("wsprobe: modem no answer after the sleep CHECK\n");
        }
        stage = 2;
        sleep_now("the modem's supply on and the modem powered down (AT+CPOF)");
    }
    if (stage == 2) {
        printf("wsprobe: woke from the sleep with the modem powered down\n");
        modem_pwr(false);
        gpio_hold_dis(PIN_MODEM_PWR);
        printf("wsprobe: done; the two currents are NFR-35.3's figures\n");
        stage = 0;
        return;
    }
    chip();
    bus();
    sd();
    modem();
    key();
    walk();
    stage = 1;
    sleep_now("the modem's supply cut (gpio33 low)");
}
