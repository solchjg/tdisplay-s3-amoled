// Commissioning aid, not product code: proves the Elecrow CrowPanel ESP32
// E-Paper 4.2" (ESP32-S3-WROOM-1-N8R8, 400 x 300 SSD1683 panel) as a
// candidate board for the Blues build, before any requirement relies on it.
// Each run (after reset or a timer wake) it
//   1. switches the panel supply on (GPIO 7 high), resets the SSD1683 and
//      checks that BUSY falls (the panel answers);
//   2. scans the Qwiic bus on two header pins (menuconfig "E-paper probe",
//      GPIO 8/9 by default): the Notecard (0x17) and the BME280 (0x76) must
//      answer, an ADS1115 (0x48 – 0x4B) is reported when fitted;
//   3. reads the BME280 once and, from the Notecard, card.version and
//      card.wireless (the signal bars);
//   4. draws the results with a full refresh and prints how long it took;
//   5. counts on the bottom line with partial refreshes, timing each;
//   6. with a sleep time set, writes the next wake time on the panel, puts it
//      in deep sleep, cuts its supply and sleeps the chip, so the last screen
//      stays visible at zero panel power while the sleep current is measured.
// Lines start with "epaperprobe:"; CHECK marks a result to look at.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bme280.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "notecard.h"
#include "sdkconfig.h"
#include "text.h"

// The CrowPanel 4.2" panel wiring (Elecrow's examples).
#define PIN_PWR 7    // panel supply enable, high = on
#define PIN_SCK 12
#define PIN_MOSI 11
#define PIN_CS 45
#define PIN_DC 46    // low = command
#define PIN_RST 47   // low = reset
#define PIN_BUSY 48  // high = busy

#define EPD_W 400
#define EPD_H 300
#define EPD_BYTES (EPD_W / 8 * EPD_H)
#define BUSY_LIMIT_MS 10000
#define SPI_CHUNK 4000

#define ADDR_BME280 0x76

static spi_device_handle_t spi;
static i2c_master_bus_handle_t bus;
static uint8_t fb[EPD_BYTES];  // 1 bit per pixel, 1 = white
static char rsp[512];
static RTC_DATA_ATTR uint32_t runs;

// ---------------------------------------------------------------- panel

static void epd_send(bool data, const uint8_t *p, size_t n)
{
    gpio_set_level(PIN_DC, data ? 1 : 0);
    while (n > 0) {
        size_t k = n > SPI_CHUNK ? SPI_CHUNK : n;
        spi_transaction_t t = {.length = k * 8, .tx_buffer = p};
        spi_device_polling_transmit(spi, &t);
        p += k;
        n -= k;
    }
}

static void epd_cmd(uint8_t c, const uint8_t *data, size_t n)
{
    epd_send(false, &c, 1);
    if (n > 0) {
        epd_send(true, data, n);
    }
}

#define CMD(c, ...)                                     \
    do {                                                \
        const uint8_t d_[] = {__VA_ARGS__};             \
        epd_cmd((c), d_, sizeof d_);                    \
    } while (0)

// Waits for BUSY low; the time it took in ms, or -1 past the limit.
static long epd_wait(void)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_BUSY) == 1) {
        if (esp_timer_get_time() - t0 > (int64_t)BUSY_LIMIT_MS * 1000) {
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return (long)((esp_timer_get_time() - t0) / 1000);
}

static esp_err_t epd_bus_start(void)
{
    gpio_hold_dis(PIN_PWR);
    gpio_config_t out = {.pin_bit_mask = (1ULL << PIN_PWR) | (1ULL << PIN_DC) | (1ULL << PIN_RST),
                         .mode = GPIO_MODE_OUTPUT};
    gpio_config(&out);
    gpio_config_t in = {.pin_bit_mask = 1ULL << PIN_BUSY, .mode = GPIO_MODE_INPUT};
    gpio_config(&in);
    gpio_set_level(PIN_PWR, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    spi_bus_config_t bc = {.sclk_io_num = PIN_SCK, .mosi_io_num = PIN_MOSI, .miso_io_num = -1,
                           .quadwp_io_num = -1, .quadhd_io_num = -1,
                           .max_transfer_sz = SPI_CHUNK};
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }
    spi_device_interface_config_t dc = {.clock_speed_hz = CONFIG_PROBE_SPI_HZ, .mode = 0,
                                        .spics_io_num = PIN_CS, .queue_size = 1};
    return spi_bus_add_device(SPI2_HOST, &dc, &spi);
}

// Hardware and software reset, then the SSD1683 setup for a 400 x 300 panel
// filled left to right, top to bottom. The reset time in ms, or -1.
static long epd_init(void)
{
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    epd_cmd(0x12, NULL, 0);  // software reset
    long ms = epd_wait();
    if (ms < 0) {
        return -1;
    }
    CMD(0x01, (EPD_H - 1) & 0xFF, (EPD_H - 1) >> 8, 0x00);  // gates
    CMD(0x3C, 0x05);                                        // border follows white
    CMD(0x18, 0x80);                                        // internal temperature sensor
    CMD(0x11, 0x03);                                        // X then Y increment
    CMD(0x44, 0x00, EPD_W / 8 - 1);
    CMD(0x45, 0x00, 0x00, (EPD_H - 1) & 0xFF, (EPD_H - 1) >> 8);
    return ms;
}

static void epd_write(uint8_t ram)
{
    CMD(0x4E, 0x00);
    CMD(0x4F, 0x00, 0x00);
    epd_cmd(ram, fb, sizeof fb);
}

// Full refresh (0xF7); afterwards both RAMs hold the image, so a partial
// refresh compares against it. Time in ms, or -1.
static long epd_full(void)
{
    epd_write(0x24);
    epd_write(0x26);
    CMD(0x21, 0x40, 0x00);  // red RAM bypassed as 0
    CMD(0x22, 0xF7);
    epd_cmd(0x20, NULL, 0);
    return epd_wait();
}

// Partial refresh (0xFC): only pixels that differ from the old RAM change.
static long epd_partial(void)
{
    epd_write(0x24);
    CMD(0x21, 0x00, 0x00);
    CMD(0x22, 0xFC);
    epd_cmd(0x20, NULL, 0);
    long ms = epd_wait();
    epd_write(0x26);  // the base for the next partial refresh
    return ms;
}

static void epd_off(void)
{
    CMD(0x10, 0x01);  // deep sleep mode 1, RAM kept until power goes
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_DC, 0);   // no signal may feed the unpowered panel
    gpio_set_level(PIN_RST, 0);
    gpio_set_level(PIN_PWR, 0);
    gpio_hold_en(PIN_PWR);
}

// ---------------------------------------------------------------- drawing

static void px(int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= EPD_W || y >= EPD_H) {
        return;
    }
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    if (black) {
        fb[y * (EPD_W / 8) + x / 8] &= (uint8_t)~bit;
    } else {
        fb[y * (EPD_W / 8) + x / 8] |= bit;
    }
}

static void clear_rows(int y0, int h)
{
    memset(fb + y0 * (EPD_W / 8), 0xFF, (size_t)h * (EPD_W / 8));
}

static void frame(void)
{
    for (int x = 0; x < EPD_W; x++) {
        px(x, 0, true);
        px(x, 1, true);
        px(x, EPD_H - 2, true);
        px(x, EPD_H - 1, true);
    }
    for (int y = 0; y < EPD_H; y++) {
        px(0, y, true);
        px(1, y, true);
        px(EPD_W - 2, y, true);
        px(EPD_W - 1, y, true);
    }
}

// One line of text in the 12x24 or 16x32 font at (x, y), black on white.
static void draw(const text_font_t *f, int x, int y, const char *s)
{
    static uint16_t line[EPD_W * FONT_H];
    int w = text_font_width(f, s);
    if (w > EPD_W - x) {
        w = EPD_W - x;
    }
    if (w <= 0) {
        return;
    }
    text_font_render(f, s, 1, 0, line, w);
    for (int r = 0; r < f->h; r++) {
        for (int c = 0; c < w; c++) {
            px(x + c, y + r, line[r * w + c] != 0);
        }
    }
}

// ---------------------------------------------------------------- Qwiic bus

static bool present(uint8_t addr)
{
    return i2c_master_probe(bus, addr, 50) == ESP_OK;
}

static void scan(char *out, size_t n)
{
    size_t o = (size_t)snprintf(out, n, "i2c:");
    printf("epaperprobe: scan sda=%d scl=%d:", CONFIG_PROBE_SDA, CONFIG_PROBE_SCL);
    for (int a = 0x08; a < 0x78 && o < n; a++) {
        if (present((uint8_t)a)) {
            printf(" 0x%02x", a);
            o += (size_t)snprintf(out + o, n - o, " %02x", a);
        }
    }
    printf("\n");
    int ads = -1;
    for (int a = 0x48; a <= 0x4B; a++) {
        if (present((uint8_t)a)) {
            ads = a;
        }
    }
    bool nc = present(NOTECARD_I2C_ADDR), bme = present(ADDR_BME280);
    printf("epaperprobe: notecard=%s bme280=%s ads1115=%s %s\n", nc ? "yes" : "no",
           bme ? "yes" : "no", ads < 0 ? "no" : "yes", nc && bme ? "ok" : "CHECK");
}

static bool ask(const char *name)
{
    char line[64];
    if (!nc_req(line, sizeof line, name)) {
        return false;
    }
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = notecard_request(line, rsp, sizeof rsp);
    long ms = (long)((esp_timer_get_time() - t0) / 1000);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        printf("epaperprobe: %s failed=%s ms=%ld CHECK\n", name, esp_err_to_name(err), ms);
        return false;
    }
    printf("epaperprobe: %s ms=%ld %s\n", name, ms, rsp);
    return !nc_err(rsp, NULL, 0);
}

// ---------------------------------------------------------------- run

void app_main(void)
{
    runs++;
    bool timer = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    printf("epaperprobe: run=%" PRIu32 " wake=%s\n", runs, timer ? "timer" : "reset");

    esp_err_t err = epd_bus_start();
    long reset_ms = err == ESP_OK ? epd_init() : -1;
    printf("epaperprobe: panel spi=%s reset_ms=%ld %s\n", esp_err_to_name(err), reset_ms,
           reset_ms >= 0 ? "ok" : "CHECK (BUSY never fell)");

    i2c_master_bus_config_t bc = {.i2c_port = 0, .sda_io_num = CONFIG_PROBE_SDA,
                                  .scl_io_num = CONFIG_PROBE_SCL,
                                  .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7};
    printf("epaperprobe: bus=%s\n", esp_err_to_name(i2c_new_master_bus(&bc, &bus)));
    char found[96];
    scan(found, sizeof found);

    char l_bme[48] = "BME280: none";
    bme280_config_t bcfg = {.i2c_port = 0, .address = ADDR_BME280, .clk_hz = 100000,
                            .settings = BME280_SETTINGS_WEATHER};
    bme280_reading_t r;
    err = bme280_start(&bcfg);
    if (err == ESP_OK && (err = bme280_read(&r)) == ESP_OK) {
        int f10 = (int)((r.temp_centi_c * 18 + 3200 * 10 + 50) / 100);  // 0.1 °F
        unsigned rh10 = (unsigned)((r.hum_q10_pct * 10 + 512) / 1024);
        unsigned mb10 = (unsigned)((r.press_q8_pa / 256 + 5) / 10);  // 0.1 mbar
        snprintf(l_bme, sizeof l_bme, "%d.%dF %u.%u%% %u.%umb", f10 / 10, f10 % 10, rh10 / 10,
                 rh10 % 10, mb10 / 10, mb10 % 10);
    }
    printf("epaperprobe: bme280=%s %s\n", esp_err_to_name(err), l_bme);

    char l_nc[48] = "Notecard: none", l_bars[32] = "bars: ?";
    notecard_config_t ncfg = {.i2c_port = 0, .address = NOTECARD_I2C_ADDR, .clk_hz = 100000,
                              .timeout_s = 10};
    if (notecard_start(&ncfg) == ESP_OK) {
        char v[40];
        if (ask("card.version") && nc_str(rsp, "version", v, sizeof v)) {
            snprintf(l_nc, sizeof l_nc, "%.38s", v);
        }
        double bars;
        if (ask("card.wireless") && nc_num(rsp, "bars", &bars)) {
            snprintf(l_bars, sizeof l_bars, "bars: %d", (int)bars);
        }
    }

    if (reset_ms < 0) {
        printf("epaperprobe: no panel, nothing drawn CHECK\n");
    } else {
        char l_run[48];
        snprintf(l_run, sizeof l_run, "run %" PRIu32 " (%s)", runs, timer ? "timer" : "reset");
        memset(fb, 0xFF, sizeof fb);
        frame();
        draw(&text_font_large, 12, 10, "CrowPanel 4.2 probe");
        draw(&text_font_small, 12, 52, l_run);
        draw(&text_font_large, 12, 84, l_bme);
        draw(&text_font_small, 12, 124, l_nc);
        draw(&text_font_small, 12, 152, l_bars);
        draw(&text_font_small, 12, 180, found);
        long ms = epd_full();
        printf("epaperprobe: full refresh ms=%ld %s\n", ms, ms >= 0 ? "ok" : "CHECK");

        for (int i = 1; i <= CONFIG_PROBE_PARTIALS; i++) {
            char l[32];
            snprintf(l, sizeof l, "partial %d of %d", i, CONFIG_PROBE_PARTIALS);
            clear_rows(220, 32);
            draw(&text_font_large, 12, 220, l);
            ms = epd_partial();
            printf("epaperprobe: partial %d ms=%ld %s\n", i, ms, ms >= 0 ? "ok" : "CHECK");
        }
    }

    if (CONFIG_PROBE_SLEEP_S == 0) {
        printf("epaperprobe: sleep off, staying awake\n");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }
    if (reset_ms >= 0) {
        char l[40];
        snprintf(l, sizeof l, "asleep, wake in %d s", CONFIG_PROBE_SLEEP_S);
        clear_rows(220, 32);
        draw(&text_font_large, 12, 220, l);
        long ms = epd_partial();
        printf("epaperprobe: sleep screen ms=%ld\n", ms);
        epd_off();
    } else {
        gpio_set_level(PIN_PWR, 0);
        gpio_hold_en(PIN_PWR);
    }
    printf("epaperprobe: panel off, deep sleep %d s; measure the current now\n",
           CONFIG_PROBE_SLEEP_S);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_sleep_enable_timer_wakeup((uint64_t)CONFIG_PROBE_SLEEP_S * 1000000ULL);
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
}
