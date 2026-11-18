// fs3-survey: a site survey on the FeatherS3D, for a field demo. Each boot or
// key wake: the display supply (the Pololu switch, GPIO 17) and LDO2 on; the
// microSD mounted (SPI2: SO 37, CS 38); the panel found (the 2.13" e-Paper HAT,
// V3, on SPI2 when its BUSY is driven, else a 1.3" SSD1306 OLED at 0x3C on
// I2C2); the battery from the MAX17048; then through the Notecard on I2C1 a GPS
// fix (continuous mode, up to the gps_min setting) and a Notehub sync with the network
// it registers on (card.wireless, up to CELL_LIMIT_S), and with a Starnote the
// same note sent over satellite (card.transport ntn, up to sat_min, when sat is on); the
// BME280 on I2C2 adds the weather. The result goes on the
// panel and as a line in /survey.csv with the local date and time from the
// Notecard. Then, with the e-paper, the panel's deep sleep, every panel and card
// line an input and the supplies off, and deep sleep until the user key (GPIO 1)
// or BOOT (GPIO 0) is pressed; with the OLED the result stays up for
// OLED_HOLD_S first. On a power-up or reset a Bluetooth setup window comes
// first (ble_window). An off-board key may sit beside the user key on GPIO 1, and
// its LED on GPIO 13 (with the board's blue LED): on while surveying, blinking on
// the result, off asleep. Serial lines start "survey:". Never released.
//
// The Notecard can neither choose nor scan operators, so the cellular line is
// the one network its SIM registered on. Without a satellite radio (a Notecard
// for Skylo, or a Starnote answering ntn.status) the
// satellite line reads n/a. The Notecard's transport is put back afterwards.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ble_setup.h"
#include "bme280.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "max17048.h"
#include "notecard.h"
#include "sdmmc_cmd.h"
#include "survey_core.h"
#include "text.h"

// FSD §40.2 pins
#define PIN_SUPPLY 17  // the Pololu switch's CTRL: high on, low off, then floating
#define PIN_LDO2 39    // I2C2's 3.3 V
#define PIN_SCK 36
#define PIN_MOSI 35
#define PIN_MISO 37
#define PIN_SD_CS 38
#define PIN_CS 5  // the e-paper's
#define PIN_DC 6
#define PIN_RST 7
#define PIN_BUSY 10
#define PIN_KEY 1   // user key, active low, 10 kΩ pull-up
#define PIN_BOOT 0  // BOOT key, active low
#define PIN_LED 13  // the board's blue LED and the off-board key's LED (to GND through a resistor)
#define I2C1_SDA 8
#define I2C1_SCL 9
#define I2C2_SDA 16
#define I2C2_SCL 15

#define BLE_IDLE_S 120   // the setup window closes after this long with no phone connected
#define BLE_MAX_S 900    // and after this long in all
#define CELL_LIMIT_S 180
#define SURVEY_FILE "survey.qo"
#define SURVEY_PORT 20  // NTN needs a templated Notefile with a port
#define POLL_S 5
#define SCREEN_EVERY_S 10  // e-paper partials while searching
#define OLED_HOLD_S 600

#define SD_MOUNT "/sd"
#define CSV SD_MOUNT "/survey.csv"

// ---------------------------------------------------------------- supplies

static void supply_ctrl(int level)
{
    gpio_hold_dis(PIN_SUPPLY);
    gpio_config_t c = {.pin_bit_mask = 1ULL << PIN_SUPPLY, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(PIN_SUPPLY, level);
    vTaskDelay(pdMS_TO_TICKS(10));
    c.mode = GPIO_MODE_INPUT;
    gpio_config(&c);
}

static void ldo2(bool on)
{
    gpio_hold_dis(PIN_LDO2);
    gpio_set_level(PIN_LDO2, on);
    gpio_config_t c = {.pin_bit_mask = 1ULL << PIN_LDO2, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(PIN_LDO2, on);
}

// ---------------------------------------------------------------- e-paper (2.13" V3)

#define EPD_W 128  // the controller's RAM: 128 columns (122 shown) x 250 rows
#define EPD_H 250
#define LW 250  // drawn in landscape
#define LH 122
#define EPD_BYTES (EPD_W / 8 * EPD_H)
#define SPI_HZ 1000000
#define SPI_CHUNK 4000
#define BUSY_LIMIT_MS 10000

static spi_device_handle_t epd;
static uint8_t fb[EPD_BYTES];  // 1 = white

static void epd_send(bool data, const uint8_t *p, size_t n)
{
    gpio_set_level(PIN_DC, data ? 1 : 0);
    while (n > 0) {
        size_t k = n > SPI_CHUNK ? SPI_CHUNK : n;
        spi_transaction_t t = {.length = k * 8, .tx_buffer = p};
        spi_device_polling_transmit(epd, &t);
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

#define CMD(c, ...)                         \
    do {                                    \
        const uint8_t d_[] = {__VA_ARGS__}; \
        epd_cmd((c), d_, sizeof d_);        \
    } while (0)

static long epd_wait(void)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_BUSY) == 1) {
        if (esp_timer_get_time() - t0 > (int64_t)BUSY_LIMIT_MS * 1000) {
            return -1;
        }
        vTaskDelay(1);
    }
    return (long)((esp_timer_get_time() - t0) / 1000);
}

// Waveshare's WS_20_30 full and WF_PARTIAL waveforms for the V3 (as in
// commissioning/ws-epaper-probe): 153 bytes of LUT, then 0x3F, 0x03, 0x04 x3, 0x2C.
static const uint8_t lut_full[159] = {
    0x80, 0x4A, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x4A, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x4A, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x4A, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x02,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x22, 0x17, 0x41, 0x00, 0x32, 0x36,
};

static const uint8_t lut_partial[159] = {
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x22, 0x17, 0x41, 0x00, 0x32, 0x36,
};

static void epd_reset(void)
{
    gpio_set_level(PIN_RST, 0);
    esp_rom_delay_us(2000);
    gpio_set_level(PIN_RST, 1);
    esp_rom_delay_us(10000);
}

static long epd_lut(const uint8_t *lut)
{
    epd_cmd(0x32, lut, 153);
    long ms = epd_wait();
    CMD(0x3F, lut[153]);
    CMD(0x03, lut[154]);
    CMD(0x04, lut[155], lut[156], lut[157]);
    CMD(0x2C, lut[158]);
    return ms;
}

static void epd_window(void)
{
    CMD(0x44, 0x00, EPD_W / 8 - 1);
    CMD(0x45, 0x00, 0x00, (EPD_H - 1) & 0xFF, (EPD_H - 1) >> 8);
}

static void epd_write(uint8_t ram)
{
    CMD(0x4E, 0x00);
    CMD(0x4F, 0x00, 0x00);
    epd_cmd(ram, fb, sizeof fb);
}

static bool epd_init(void)
{
    epd_reset();
    if (epd_wait() < 0) {
        return false;
    }
    epd_cmd(0x12, NULL, 0);
    if (epd_wait() < 0) {
        return false;
    }
    CMD(0x01, (EPD_H - 1) & 0xFF, (EPD_H - 1) >> 8, 0x00);
    CMD(0x11, 0x03);
    epd_window();
    CMD(0x3C, 0x05);
    CMD(0x21, 0x00, 0x80);
    CMD(0x18, 0x80);
    return epd_wait() >= 0 && epd_lut(lut_full) >= 0;
}

static long epd_full(void)
{
    epd_write(0x24);
    epd_write(0x26);
    CMD(0x22, 0xC7);
    epd_cmd(0x20, NULL, 0);
    return epd_wait();
}

// As Waveshare's V3 code: a reset, the partial waveform, then the B/W RAM.
static long epd_partial(void)
{
    epd_reset();
    if (epd_wait() < 0) {
        return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    if (epd_lut(lut_partial) < 0) {
        return -1;
    }
    CMD(0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00);
    CMD(0x3C, 0x80);
    CMD(0x22, 0xC0);
    epd_cmd(0x20, NULL, 0);
    if (epd_wait() < 0) {
        return -1;
    }
    epd_window();
    epd_write(0x24);
    CMD(0x22, 0x0F);
    epd_cmd(0x20, NULL, 0);
    return epd_wait();
}

static void epd_px(int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= LW || y >= LH) {
        return;
    }
    // Landscape on the portrait RAM: picture x down the RAM's rows, y across its columns.
    int rx = LH - 1 - y, ry = x;
    uint8_t bit = (uint8_t)(0x80 >> (rx & 7));
    if (black) {
        fb[ry * (EPD_W / 8) + rx / 8] &= (uint8_t)~bit;
    } else {
        fb[ry * (EPD_W / 8) + rx / 8] |= bit;
    }
}

// ---------------------------------------------------------------- OLED (1.3", SSD1306, I2C2)

#define OLED_W 128
#define OLED_H 64

static i2c_master_bus_handle_t i2c2;
static i2c_master_dev_handle_t oled;
static uint8_t ob[OLED_H / 8][OLED_W];

static esp_err_t oled_cmds(const uint8_t *c, size_t n)
{
    uint8_t b[32];
    b[0] = 0x00;
    memcpy(b + 1, c, n);
    return i2c_master_transmit(oled, b, n + 1, 50);
}

static bool oled_find(void)
{
    const i2c_master_bus_config_t bc = {.i2c_port = 1, .sda_io_num = I2C2_SDA, .scl_io_num = I2C2_SCL,
                                        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                        .flags.enable_internal_pullup = true};
    if (i2c_new_master_bus(&bc, &i2c2) != ESP_OK) {
        return false;
    }
    for (uint16_t a = 0x3C; a <= 0x3D; a++) {
        if (i2c_master_probe(i2c2, a, 50) == ESP_OK) {
            const i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = a,
                                            .scl_speed_hz = 400000};
            if (i2c_master_bus_add_device(i2c2, &dc, &oled) != ESP_OK) {
                break;
            }
            static const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
                                           0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
                                           0xDB, 0x40, 0xA4, 0xA6, 0xAF};
            printf("survey: OLED at 0x%02X\n", a);
            return oled_cmds(init, sizeof init) == ESP_OK;
        }
    }
    i2c_del_master_bus(i2c2);
    i2c2 = NULL;
    return false;
}

static void oled_px(int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) {
        return;
    }
    if (on) {
        ob[y / 8][x] |= (uint8_t)(1 << (y & 7));
    } else {
        ob[y / 8][x] &= (uint8_t)~(1 << (y & 7));
    }
}

static esp_err_t oled_push(void)
{
    esp_err_t err = ESP_OK;
    static uint8_t page[1 + OLED_W];
    for (int p = 0; err == ESP_OK && p < OLED_H / 8; p++) {
        const uint8_t at[] = {(uint8_t)(0xB0 + p), 0x00, 0x10};
        err = oled_cmds(at, sizeof at);
        page[0] = 0x40;
        memcpy(page + 1, ob[p], OLED_W);
        if (err == ESP_OK) {
            err = i2c_master_transmit(oled, page, sizeof page, 50);
        }
    }
    return err;
}

static void oled_off(void)
{
    if (oled) {
        const uint8_t off[] = {0xAE};
        oled_cmds(off, sizeof off);
    }
}

// ---------------------------------------------------------------- the screen

typedef enum { DISP_NONE, DISP_EPD, DISP_OLED } disp_t;
static disp_t disp = DISP_NONE;
static bool epd_drawn;  // a full refresh has set the base picture
static bool epd_ok, oled_ok;  // each panel found and set up
static sv_settings_t set;  // from NVS, changed over Bluetooth

// The panel to draw on, from the `display` setting and the panels found; the
// OLED is turned on only while chosen.
static void display_choose(void)
{
    int p = sv_display_pick(set.display, epd_ok, oled_ok);
    disp_t was = disp;
    disp = p == SV_DISP_EPAPER ? DISP_EPD : p == SV_DISP_OLED ? DISP_OLED : DISP_NONE;
    if (oled_ok && disp != was) {
        const uint8_t on[] = {disp == DISP_OLED ? 0xAF : 0xAE};
        oled_cmds(on, sizeof on);
    }
    printf("survey: display %s (setting %d, e-paper %s, OLED %s)\n",
           disp == DISP_EPD ? "e-paper 2.13in" : disp == DISP_OLED ? "OLED" : "none", set.display,
           epd_ok ? "found" : "absent", oled_ok ? "found" : "absent");
}

static void draw_text(const text_font_t *f, int x, int y, const char *s)
{
    static uint16_t line[LW * FONT_H];
    int maxw = disp == DISP_EPD ? LW : OLED_W;
    int w = text_font_width(f, s);
    if (w > maxw - x) {
        w = maxw - x;
    }
    if (w <= 0) {
        return;
    }
    text_font_render(f, s, 1, 0, line, w);
    for (int r = 0; r < f->h; r++) {
        for (int c = 0; c < w; c++) {
            if (disp == DISP_EPD) {
                epd_px(x + c, y + r, line[r * w + c] != 0);
            } else {
                oled_px(x + c, y + r, line[r * w + c] != 0);
            }
        }
    }
}

// The title and six lines; full forces an e-paper full refresh.
static void show(const char *title, char lines[SV_LINES][SV_LINE_MAX], bool full)
{
    for (int i = 0; i < SV_LINES; i++) {
        printf("survey: | %s\n", lines[i]);
    }
    if (disp == DISP_EPD) {
        memset(fb, 0xFF, sizeof fb);
        // The title, a rule, then the lines 14 px apart (8 x 16 glyphs, the
        // last line's descenders at the bottom edge).
        draw_text(&text_font_mini, 2, 0, title);
        for (int x = 0; x < LW; x++) {
            epd_px(x, 16, true);
        }
        for (int i = 0; i < SV_LINES; i++) {
            draw_text(&text_font_mini, 2, 18 + i * 15, lines[i]);
        }
        bool use_full = full || !epd_drawn;
        if (use_full && epd_drawn) {
            epd_init();  // the partials left their waveform loaded
        }
        long ms = use_full ? epd_full() : epd_partial();
        epd_drawn = true;
        printf("survey: e-paper %s %ld ms\n", use_full ? "full" : "partial", ms);
    } else if (disp == DISP_OLED) {
        memset(ob, 0, sizeof ob);
        for (int i = 0; i < SV_LINES; i++) {
            draw_text(&text_font_tiny, 0, i * 9, lines[i]);
        }
        oled_push();
    }
}

// ---------------------------------------------------------------- microSD

static sdmmc_card_t *card;

static bool sd_mount(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SPI2_HOST;
    const esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 2,
                                                 .allocation_unit_size = 0};
    esp_err_t err = ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(250));  // 350 ms after the supply in all
    for (int i = 0; i < 3 && err != ESP_OK; i++) {
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mc, &card);
        printf("survey: sd mount attempt %d %s\n", i + 1, esp_err_to_name(err));
    }
    printf("survey: sd mount %s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        card = NULL;
    }
    return err == ESP_OK;
}

static bool sd_append(const sv_result_t *r)
{
    if (!card) {
        return false;
    }
    char line[256], head[256];
    struct stat st;
    bool fresh = stat(CSV, &st) != 0;
    if (!fresh) {
        // A file with another header (an older column set) is kept under a
        // free name, survey-1.csv to survey-9.csv (8.3 names), and a new one started.
        FILE *old = fopen(CSV, "r");
        bool same = false;
        if (old) {
            size_t hn = sv_csv_header(line, sizeof line);
            same = hn > 0 && fgets(head, sizeof head, old) && strcmp(head, line) == 0;
            fclose(old);
        }
        for (int i = 1; !same && i < 10; i++) {
            char to[32];
            snprintf(to, sizeof to, SD_MOUNT "/survey-%d.csv", i);
            if (stat(to, &st) != 0) {
                printf("survey: csv header changed, old file kept as %s\n", to);
                fresh = rename(CSV, to) == 0;
                break;
            }
        }
    }
    FILE *f = fopen(CSV, "a");
    if (!f) {
        return false;
    }
    bool ok = true;
    if (fresh) {
        size_t n = sv_csv_header(line, sizeof line);
        ok = n > 0 && fwrite(line, 1, n, f) == n;
    }
    size_t n = sv_csv_line(r, line, sizeof line);
    ok = ok && n > 0 && fwrite(line, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    printf("survey: csv %s: %s", ok ? "written" : "FAILED", line);
    return ok;
}

// ---------------------------------------------------------------- Notecard

static char rsp[1024];

// Sends req (one JSON line) and prints the answer; false on a bus error or an "err".
static bool nc_send(const char *req)
{
    esp_err_t err = notecard_request(req, rsp, sizeof rsp);
    printf("survey: > %s", req);
    printf("survey: < %s\n", err == ESP_OK ? rsp : esp_err_to_name(err));
    return err == ESP_OK && !nc_err(rsp, NULL, 0);
}

static bool nc_name(const char *name)
{
    char req[64];
    return nc_req(req, sizeof req, name) > 0 && nc_send(req);
}

// Epoch seconds and the zone's offset in minutes from card.time; false before the time is set.
static bool nc_time(int64_t *epoch, int *minutes)
{
    double t, m = 0;
    if (!nc_name("card.time") || !nc_num(rsp, "time", &t) || t < 1.6e9) {
        return false;
    }
    nc_num(rsp, "minutes", &m);
    *epoch = (int64_t)t;
    *minutes = (int)m;
    return true;
}

static int key_down(void)
{
    return gpio_get_level(PIN_KEY) == 0 || gpio_get_level(PIN_BOOT) == 0;
}

// A real press: a key held low through 100 ms of samples, so a glitch on a
// long off-board key cable does not end a step.
static bool key_held(void)
{
    for (int i = 0; i < 10; i++) {
        if (!key_down()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

// One poll interval, cut short by a key press: true once a key was pressed
// (and let go, so the press does not also restart the survey at the end).
static bool poll_wait(void)
{
    for (int i = 0; i < POLL_S * 10; i++) {
        if (key_down()) {
            if (key_held()) {
                printf("survey: key pressed, step skipped\n");
                while (key_down()) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

// ---------------------------------------------------------------- the survey

static sv_result_t res;
static bool survey_note(void);

static void progress(const char *gps, const char *cell, bool full)
{
    char lines[SV_LINES][SV_LINE_MAX];
    sv_result_lines(&res, lines);
    snprintf(lines[0], SV_LINE_MAX, "%s", gps);
    snprintf(lines[1], SV_LINE_MAX, "%s", cell);
    lines[5][0] = '\0';
    snprintf(lines[6], SV_LINE_MAX, "Surveying - key: skip");
    show(res.site[0] ? res.site : "Site survey", lines, full);
}

static void gps_phase(int64_t t_start)
{
    char saved[24] = "";
    if (nc_name("card.location.mode")) {
        nc_str(rsp, "mode", saved, sizeof saved);
    }
    char req[128];
    if (nc_req_location_mode(req, sizeof req, "continuous", NULL, NULL) > 0) {
        nc_send(req);
    }
    int64_t t0 = esp_timer_get_time();
    int64_t shown = t0;
    char line[SV_LINE_MAX];
    // The Notecard keeps its last fix: a fix counts only once its time is
    // newer than the one card.location reported first (and than the clock at
    // the start, when the clock was set).
    double stale = -1;
    for (;;) {
        int s = (int)((esp_timer_get_time() - t0) / 1000000);
        double lat, lon, when = 0;
        bool got = nc_name("card.location");
        bool has = got && nc_num(rsp, "lat", &lat) && nc_num(rsp, "lon", &lon);
        if (has) {
            nc_num(rsp, "time", &when);
        }
        if (got && stale < 0) {
            stale = has ? when : 0;
            printf("survey: last stored fix time %.0f\n", stale);
        } else if (has && when > stale && (t_start == 0 || when + 60 >= (double)t_start)) {
            {
                res.gps_fix = true;
                res.lat = lat;
                res.lon = lon;
                res.gps_s = s;
                break;
            }
        }
        if (s >= set.gps_min * 60) {
            res.gps_s = s;
            break;
        }
        if (esp_timer_get_time() - shown >= (int64_t)SCREEN_EVERY_S * 1000000) {
            shown = esp_timer_get_time();
            snprintf(line, sizeof line, "GPS: searching %ds", s);
            progress(line, "Cell: waiting", false);
        }
        if (poll_wait()) {
            res.gps_s = (int)((esp_timer_get_time() - t0) / 1000000);
            break;
        }
    }
    // Back to the mode it had (none set means off).
    if (nc_req_location_mode(req, sizeof req, saved[0] && strcmp(saved, "continuous") != 0 ? saved : "off",
                             NULL, NULL) > 0) {
        nc_send(req);
    }
}

static void cell_phase(int64_t t_start, const char *gps_line)
{
    char req[96];
    if (nc_req_transport(req, sizeof req, "cell") > 0) {
        nc_send(req);
    }
    survey_note();
    nc_name("hub.sync");
    int64_t t0 = esp_timer_get_time();
    int64_t shown = t0;
    int seen_s = -1;
    char line[SV_LINE_MAX];
    for (;;) {
        int s = (int)((esp_timer_get_time() - t0) / 1000000);
        double v;
        // Values from the satellite identity (rat "NTN ...", Skylo's mcc 901) stay
        // in card.wireless after an ntn session: not a cellular network.
        char rat[24] = "";
        bool ntn_left = false;
        if (nc_name("card.wireless")) {
            nc_str(rsp, "net.rat", rat, sizeof rat);
            double mcc = 0;
            ntn_left = strncmp(rat, "NTN", 3) == 0 || strncmp(rat, "ntn", 3) == 0 ||
                       (nc_num(rsp, "net.mcc", &mcc) && (int)mcc == 901);
        }
        if (!ntn_left && nc_num(rsp, "net.rssi", &v) && v != 0) {
            double upd = 0;
            nc_num(rsp, "net.updated", &upd);
            if (t_start == 0 || upd == 0 || upd + 60 >= (double)t_start) {
                if (seen_s < 0) {
                    seen_s = s;
                }
                int bars = nc_num(rsp, "net.bars", &v) ? (int)v : 0;
                if (!res.cell || bars >= res.bars) {
                    res.cell = true;
                    res.bars = bars;
                    res.rssi = nc_num(rsp, "net.rssi", &v) ? (int)v : 0;
                    res.rsrp = nc_num(rsp, "net.rsrp", &v) ? (int)v : 0;
                    res.mcc = nc_num(rsp, "net.mcc", &v) ? (int)v : 0;
                    res.mnc = nc_num(rsp, "net.mnc", &v) ? (int)v : 0;
                    nc_str(rsp, "net.rat", res.rat, sizeof res.rat);
                }
            }
        }
        // A sync completed since this one was asked for ("completed": seconds ago).
        if (nc_name("hub.sync.status") && nc_num(rsp, "completed", &v) && v <= s + POLL_S) {
            res.synced = true;
        }
        // Done once synced with a network seen, or at the limit (a registration
        // and sync took 50 - 110 s on the bench, more after an ntn session).
        if ((res.cell && res.synced) || s >= CELL_LIMIT_S) {
            res.cell_s = seen_s >= 0 ? seen_s : s;
            break;
        }
        if (esp_timer_get_time() - shown >= (int64_t)SCREEN_EVERY_S * 1000000) {
            shown = esp_timer_get_time();
            snprintf(line, sizeof line, res.cell ? "Cell: syncing %ds" : "Cell: searching %ds", s);
            progress(gps_line, line, false);
        }
        if (poll_wait()) {
            res.cell_s = seen_s >= 0 ? seen_s : (int)((esp_timer_get_time() - t0) / 1000000);
            break;
        }
    }
}

// The survey's note: a compact template (port SURVEY_PORT, so NTN takes it),
// then one note with what was found so far; sync says whether to send it now.
static bool survey_note(void)
{
    static bool templated;
    char req[512];
    if (!templated) {
        snprintf(req, sizeof req,
                 "{\"req\":\"note.template\",\"file\":\"" SURVEY_FILE "\",\"port\":%d,"
                 "\"format\":\"compact\",\"body\":{\"lat\":14.1,\"lon\":14.1,\"bars\":11,"
                 "\"rsrp\":12,\"temp\":12,\"batt\":12}}\n",
                 SURVEY_PORT);
        templated = nc_send(req);
    }
    snprintf(req, sizeof req,
             "{\"req\":\"note.add\",\"file\":\"" SURVEY_FILE "\",\"body\":{\"lat\":%.5f,\"lon\":%.5f,"
             "\"bars\":%d,\"rsrp\":%d,\"temp\":%d,\"batt\":%d}}\n",
             res.gps_fix ? res.lat : 0.0, res.gps_fix ? res.lon : 0.0, res.bars, res.rsrp,
             res.wx ? res.temp_centi_c : 0, res.batt_mv);
    return nc_send(req);
}

// A sync forced and waited for, up to limit_s: true once the Notecard reports
// a sync completed since this one was asked for.
static bool sync_wait(int limit_s, int *took_s, const char *gps_line, const char *cell_line)
{
    nc_name("hub.sync");
    int64_t t0 = esp_timer_get_time(), shown = t0;
    char line[SV_LINE_MAX];
    for (;;) {
        bool skip = poll_wait();
        int s = (int)((esp_timer_get_time() - t0) / 1000000);
        double done;
        if (nc_name("hub.sync.status")) {
            char st[96] = "";
            bool pending = strstr(rsp, "\"sync\":true") != NULL;
            nc_str(rsp, "status", st, sizeof st);
            if (strstr(st, "{sync-end}") || (!pending && nc_num(rsp, "completed", &done) && done <= s)) {
                *took_s = s;
                return true;
            }
        }
        if (s >= limit_s || skip) {
            *took_s = s;
            return false;
        }
        if (esp_timer_get_time() - shown >= (int64_t)SCREEN_EVERY_S * 1000000) {
            shown = esp_timer_get_time();
            snprintf(line, sizeof line, "Satellite: %d:%02d", s / 60, s % 60);
            char lines[SV_LINES][SV_LINE_MAX];
            sv_result_lines(&res, lines);
            snprintf(lines[0], SV_LINE_MAX, "%s", gps_line);
            snprintf(lines[1], SV_LINE_MAX, "%s", cell_line);
            snprintf(lines[2], SV_LINE_MAX, "%s", line);
            lines[5][0] = '\0';
            snprintf(lines[6], SV_LINE_MAX, "Surveying - key: skip");
            show(res.site[0] ? res.site : "Site survey", lines, false);
        }
    }
}

static bool has_ntn;  // card.version's "ntn":true

static void sat_phase(const char *gps_line, const char *cell_line)
{
    // A Notecard for Skylo (card.version "ntn":true) or a paired Starnote
    // (ntn.status without {no-ntn-module}); with neither the line stays n/a.
    if (!has_ntn && (!nc_name("ntn.status") || strstr(rsp, "{no-ntn-module}") ||
                     strstr(rsp, "{ntn-unknown}") || strstr(rsp, "{ntn-none}"))) {
        printf("survey: no satellite radio, satellite skipped\n");
        return;
    }
    char req[96];
    if (nc_req_transport(req, sizeof req, "ntn") == 0 || !nc_send(req)) {
        return;
    }
    res.sat_tried = true;
    survey_note();
    res.sat = sync_wait(set.sat_min * 60, &res.sat_s, gps_line, cell_line);
}

static void lines_inputs(void)
{
    gpio_config_t in = {.pin_bit_mask = (1ULL << PIN_SCK) | (1ULL << PIN_MOSI) | (1ULL << PIN_MISO) |
                                        (1ULL << PIN_CS) | (1ULL << PIN_SD_CS) | (1ULL << PIN_DC) |
                                        (1ULL << PIN_RST),
                        .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&in);
}

static void led(bool on)
{
    gpio_set_level(PIN_LED, on);
}

// The LED off and its line an input (no current) for the deep sleep.
static void led_release(void)
{
    gpio_config_t in = {.pin_bit_mask = 1ULL << PIN_LED, .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&in);
}

static void sleep_until_key(void)
{
    led_release();
    printf("survey: asleep until the user key or BOOT\n");
    fflush(stdout);
    rtc_gpio_pullup_en(PIN_KEY);
    rtc_gpio_pulldown_dis(PIN_KEY);
    rtc_gpio_pullup_en(PIN_BOOT);
    rtc_gpio_pulldown_dis(PIN_BOOT);
    esp_sleep_enable_ext1_wakeup((1ULL << PIN_KEY) | (1ULL << PIN_BOOT), ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------- Bluetooth setup
// On a power-up or reset (not a key wake) the shared ble_setup service runs
// first: the phone page (shared/components/ble_setup/web/index.html) reads and
// writes the survey's settings and downloads survey.csv. A key press, the
// page's Done, BLE_IDLE_S without a phone or BLE_MAX_S in all end it.

#define NVS_NS "survey"

static void settings_load(void)
{
    sv_settings_default(&set);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        sv_settings_t s;
        size_t n = sizeof s;
        if (nvs_get_blob(h, "set", &s, &n) == ESP_OK && n == sizeof s && s.gps_min >= 1 && s.gps_min <= 10 &&
            s.sat_min >= 1 && s.sat_min <= 15) {
            s.site[SV_SITE_MAX] = '\0';
            set = s;
        }
        nvs_close(h);
    }
    snprintf(res.site, sizeof res.site, "%s", set.site);
    printf("survey: settings site='%s' sat=%s gps_min=%d sat_min=%d\n", set.site, set.sat ? "on" : "off",
           set.gps_min, set.sat_min);
}

static bool settings_save(const sv_settings_t *s)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_blob(h, "set", s, sizeof *s) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static char ble_name[16];
#define RESTART_MAGIC 0x53525652u  // set before the result screen's restart: no setup window
RTC_NOINIT_ATTR static uint32_t restart_mark;
static volatile uint32_t ble_code;      // the passkey on show, or 0
static volatile bool ble_code_on, ble_conn, ble_done, ble_dirty, display_changed;
static char hist_from[11], hist_to[11];  // the page's date range, "" for open

static bool put_raw(char *out, size_t cap, size_t *p, const char *s)
{
    size_t n = strlen(s);
    if (*p + n >= cap) {
        return false;
    }
    memcpy(out + *p, s, n + 1);
    *p += n;
    return true;
}

static size_t ble_read_info(char *out, size_t cap)
{
    char head[256], v[48];
    size_t p = 0, hn = sv_csv_header(head, sizeof head);
    if (hn > 0) {
        head[hn - 1] = '\0';  // the newline
    }
    bool ok = put_raw(out, cap, &p, "name=") && sv_url_put(out, cap, &p, ble_name);
    if (ok && res.batt_mv >= 0) {
        snprintf(v, sizeof v, "%d.%02d V %d %%", res.batt_mv / 1000, res.batt_mv % 1000 / 10, res.batt_pct);
        ok = put_raw(out, cap, &p, "&battery=") && sv_url_put(out, cap, &p, v);
    }
    if (ok && card) {
        ok = put_raw(out, cap, &p, "&history=") && sv_url_put(out, cap, &p, head);
    }
    return ok ? p : 0;
}

static size_t ble_read_settings(char *out, size_t cap)
{
    return sv_settings_form(&set, out, cap);
}

static size_t ble_read_form(char *out, size_t cap)
{
    return sv_settings_rules(out, cap);
}

static void ble_apply(const char *body, size_t len, char *result, size_t cap)
{
    sv_settings_t s;
    char bad[16];
    if (!sv_settings_parse(body, len, &set, &s, bad, sizeof bad)) {
        snprintf(result, cap, "invalid=%s", bad);
        printf("survey: ble settings invalid=%s\n", bad);
        return;
    }
    set = s;
    snprintf(res.site, sizeof res.site, "%s", set.site);
    snprintf(result, cap, "%s", settings_save(&set) ? "ok" : "invalid=site");
    printf("survey: ble settings applied site='%s' sat=%s gps_min=%d sat_min=%d\n", set.site,
           set.sat ? "on" : "off", set.gps_min, set.sat_min);
    display_changed = true;
    ble_dirty = true;
}

static void ble_passkey(uint32_t passkey, bool shown)
{
    ble_code = passkey;
    ble_code_on = shown;
    ble_dirty = true;
}

static void ble_event(ble_setup_event_t ev)
{
    if (ev == BLE_SETUP_EV_CONNECTED) {
        ble_conn = true;
    } else if (ev == BLE_SETUP_EV_DISCONNECTED) {
        ble_conn = false;
        ble_code_on = false;
    } else if (ev == BLE_SETUP_EV_DONE) {
        ble_done = true;
    } else if (ev == BLE_SETUP_EV_PAIRED) {
        ble_code_on = false;
    }
    ble_dirty = true;
}

// Whole CSV lines from data line `line` (the header not counted) within the
// page's range, as many as fit in cap.
static size_t ble_read_history(uint32_t line, char *out, size_t cap)
{
    if (!card) {
        return 0;
    }
    FILE *f = fopen(CSV, "r");
    if (!f) {
        return 0;
    }
    char buf[256];
    size_t p = 0;
    uint32_t n = 0;
    bool first = true;
    while (fgets(buf, sizeof buf, f)) {
        if (first) {
            first = false;  // the header
            continue;
        }
        if (!sv_in_range(buf, hist_from, hist_to)) {
            continue;
        }
        if (n++ < line) {
            continue;
        }
        size_t l = strlen(buf);
        if (p + l > cap) {
            break;
        }
        memcpy(out + p, buf, l);
        p += l;
    }
    fclose(f);
    return p;
}

static bool ble_history_range(const char *text, size_t len)
{
    char t[32];
    if (len >= sizeof t) {
        return false;
    }
    memcpy(t, text, len);
    t[len] = '\0';
    char *comma = strchr(t, ',');
    if (!comma) {
        return false;
    }
    *comma = '\0';
    snprintf(hist_from, sizeof hist_from, "%.10s", t);
    snprintf(hist_to, sizeof hist_to, "%.10s", comma + 1);
    return true;
}

static void ble_window(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(ble_name, sizeof ble_name, "FS3-%02X%02X%02X", mac[3], mac[4], mac[5]);
    static const ble_setup_config_t cfg = {
        .name = ble_name,
        .read_info = ble_read_info,
        .read_settings = ble_read_settings,
        .read_form = ble_read_form,
        .on_apply = ble_apply,
        .on_passkey = ble_passkey,
        .on_event = ble_event,
        .read_history = ble_read_history,
        .on_history_range = ble_history_range,
    };
    esp_err_t err = ble_setup_start(&cfg);
    printf("survey: ble setup %s as %s\n", esp_err_to_name(err), ble_name);
    if (err != ESP_OK) {
        return;
    }
    int64_t t0 = esp_timer_get_time(), idle0 = t0;
    int shown_s = -1;
    bool was_conn = false;
    ble_dirty = true;
    for (;;) {
        int64_t now = esp_timer_get_time();
        if (ble_conn) {
            idle0 = now;
        }
        int left = BLE_IDLE_S - (int)((now - idle0) / 1000000);
        if (ble_done || left <= 0 || now - t0 >= (int64_t)BLE_MAX_S * 1000000) {
            printf("survey: ble setup ends (%s)\n", ble_done ? "done" : "time");
            break;
        }
        if (key_down()) {
            if (key_held()) {
                printf("survey: ble setup ends (key)\n");
                while (key_down()) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                break;
            }
        }
        // The screen on any change, and the countdown every 10 s while waiting.
        if (display_changed) {
            display_changed = false;
            display_choose();  // in this task: the panels are driven only from here
        }
        int tick = ble_conn ? -2 : left / 10;
        if (ble_dirty || tick != shown_s || ble_conn != was_conn) {
            ble_dirty = false;
            shown_s = tick;
            was_conn = ble_conn;
            char lines[SV_LINES][SV_LINE_MAX];
            snprintf(lines[0], SV_LINE_MAX, "Name: %s", ble_name);
            if (ble_code_on) {
                snprintf(lines[1], SV_LINE_MAX, "Code: %06lu", (unsigned long)ble_code);
            } else {
                snprintf(lines[1], SV_LINE_MAX, "%s", ble_conn ? "Phone connected" : "Waiting for phone");
            }
            snprintf(lines[2], SV_LINE_MAX, "Site: %.15s", set.site[0] ? set.site : "-");
            char sat[8] = "off";
            if (set.sat) {
                snprintf(sat, sizeof sat, "%dm", set.sat_min);
            }
            snprintf(lines[3], SV_LINE_MAX, "GPS %dm  Sat %s", set.gps_min, sat);
            if (ble_conn) {
                static const char *const scr[] = {"auto", "e-paper", "OLED"};
                snprintf(lines[4], SV_LINE_MAX, "Screen: %s", scr[set.display >= 0 && set.display <= 2 ? set.display : 0]);
            } else {
                snprintf(lines[4], SV_LINE_MAX, "Closes in %ds", left);
            }
            snprintf(lines[5], SV_LINE_MAX, "Done on page or key:");
            snprintf(lines[6], SV_LINE_MAX, "start the survey");
            show("Bluetooth setup", lines, false);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ble_setup_stop();
}

void app_main(void)
{
    printf("survey: start, wake cause %d\n", (int)esp_sleep_get_wakeup_cause());
    gpio_config_t keys = {.pin_bit_mask = (1ULL << PIN_KEY) | (1ULL << PIN_BOOT), .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE};
    rtc_gpio_deinit(PIN_KEY);
    rtc_gpio_deinit(PIN_BOOT);
    gpio_config(&keys);
    // The LED on while the survey runs, blinking on the result, off asleep.
    gpio_config_t lc = {.pin_bit_mask = 1ULL << PIN_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&lc);
    led(true);
    while (key_down()) {  // the press that woke it
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nerr = nvs_flash_init();
    }
    printf("survey: nvs %s\n", esp_err_to_name(nerr));
    settings_load();

    // Supplies on (FR-40.8: 100 ms), then the card before any panel traffic (FR-40.14).
    supply_ctrl(1);
    ldo2(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_CS, 1);
    gpio_set_level(PIN_RST, 1);
    gpio_config_t out = {.pin_bit_mask = (1ULL << PIN_CS) | (1ULL << PIN_DC) | (1ULL << PIN_RST),
                         .mode = GPIO_MODE_OUTPUT};
    gpio_config(&out);
    gpio_config_t busy = {.pin_bit_mask = 1ULL << PIN_BUSY, .mode = GPIO_MODE_INPUT};
    gpio_config(&busy);
    spi_bus_config_t bc = {.sclk_io_num = PIN_SCK, .mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO,
                           .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = SPI_CHUNK};
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO);
    printf("survey: spi %s\n", esp_err_to_name(err));
    res.sd_ok = err == ESP_OK && sd_mount();

    // The panel: the e-paper when its BUSY is driven (the same with either pull), else the OLED.
    gpio_set_pull_mode(PIN_BUSY, GPIO_PULLUP_ONLY);
    esp_rom_delay_us(200);
    int up = gpio_get_level(PIN_BUSY);
    gpio_set_pull_mode(PIN_BUSY, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(200);
    int down = gpio_get_level(PIN_BUSY);
    gpio_set_pull_mode(PIN_BUSY, GPIO_FLOATING);
    if (err == ESP_OK && up == down) {
        spi_device_interface_config_t dc = {.clock_speed_hz = SPI_HZ, .mode = 0, .spics_io_num = PIN_CS,
                                            .queue_size = 1};
        epd_ok = spi_bus_add_device(SPI2_HOST, &dc, &epd) == ESP_OK && epd_init();
    }
    // Both panels may be fitted; the `display` setting picks one (auto: the e-paper).
    oled_ok = oled_find();
    disp = DISP_OLED;  // so display_choose turns a found OLED off when it is not chosen
    display_choose();

    // The Notecard and the gauge on I2C1.
    const notecard_config_t nc = {.i2c_port = 0, .sda_gpio = I2C1_SDA, .scl_gpio = I2C1_SCL,
                                  .address = NOTECARD_I2C_ADDR, .clk_hz = 100000, .internal_pullup = false,
                                  .timeout_s = 10};
    bool have_nc = notecard_start(&nc) == ESP_OK && nc_name("card.version");
    has_ntn = have_nc && strstr(rsp, "\"ntn\":true") != NULL;
    const max17048_config_t gc = {.i2c_port = 0, .sda_gpio = I2C1_SDA, .scl_gpio = I2C1_SCL,
                                  .clk_hz = 100000, .internal_pullup = false};
    max17048_reading_t b;
    res.batt_mv = -1;
    if (max17048_start(&gc) == ESP_OK && max17048_read(&b) == ESP_OK) {
        res.batt_mv = b.mv;
        res.batt_pct = b.pct;
    }
    // The Notecard's own supply (VMODEM_P, from the Notecarrier's LiPo or USB);
    // it has no gauge, so a voltage only.
    double nv;
    res.nc_mv = -1;
    if (have_nc && nc_send("{\"req\":\"card.voltage\",\"now\":true}\n") && nc_num(rsp, "value", &nv) &&
        nv > 0) {
        res.nc_mv = (int)(nv * 1000 + 0.5);
    }
    printf("survey: battery %d mV %d%%, notecard %d mV\n", res.batt_mv, res.batt_pct, res.nc_mv);

    // The weather from the BME280 on I2C2 (the OLED's bus when one is fitted),
    // at 0x76 or else 0x77 (SDO high, Adafruit's default), as FR-23.1.
    bme280_config_t wc = {.i2c_port = 1, .sda_gpio = I2C2_SDA, .scl_gpio = I2C2_SCL, .address = 0x76,
                          .clk_hz = 100000, .internal_pullup = true,
                          .settings = BME280_SETTINGS_WEATHER};
    bme280_reading_t w;
    bool wx_up = bme280_start(&wc) == ESP_OK;
    if (!wx_up) {
        wc.address = 0x77;
        wx_up = bme280_start(&wc) == ESP_OK;
    }
    printf("survey: bme280 %s at 0x%02x\n", wx_up ? "found" : "not found", wc.address);
    if (wx_up && bme280_read(&w) == ESP_OK) {
        res.wx = true;
        res.temp_centi_c = (int)w.temp_centi_c;
        res.hum_pct = (int)((w.hum_q10_pct + 512) / 1024);
        res.press_hpa = (int)((w.press_q8_pa / 256 + 50) / 100);
    }
    if (wx_up) {
        bme280_stop();
    }
    printf("survey: weather %s %d cC %d %% %d hPa\n", res.wx ? "ok" : "none", res.temp_centi_c,
           res.hum_pct, res.press_hpa);

    // Bluetooth setup on a power-up or reset, not on a key wake.
    bool again = restart_mark == RESTART_MAGIC && esp_reset_reason() == ESP_RST_SW;
    restart_mark = 0;
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1 && !again) {
        ble_window();
    }

    int64_t epoch = 0;
    int minutes = 0;
    if (have_nc) {
        nc_time(&epoch, &minutes);
    }
    progress(have_nc ? "GPS: starting" : "Notecard: none", "Cell: waiting", true);

    char transport[24] = "";
    if (have_nc && nc_name("card.transport")) {
        nc_str(rsp, "method", transport, sizeof transport);
    }
    if (have_nc && strcmp(transport, "ntn") == 0) {
        // Left by a survey cut off in its satellite step: back to cellular now,
        // so the modem's switch happens during the GPS search, not the cell step.
        char req[96];
        if (nc_req_transport(req, sizeof req, "cell") > 0) {
            nc_send(req);
        }
    }
    if (have_nc) {
        gps_phase(epoch);
        char gps_line[SV_LINE_MAX];
        if (res.gps_fix) {
            snprintf(gps_line, sizeof gps_line, "%.5f,%.5f", res.lat, res.lon);
        } else {
            snprintf(gps_line, sizeof gps_line, "GPS: no fix (%ds)", res.gps_s);
        }
        progress(gps_line, "Cell: searching", false);
        cell_phase(epoch, gps_line);
        char cell_line[SV_LINE_MAX];
        char lines0[SV_LINES][SV_LINE_MAX];
        sv_result_lines(&res, lines0);
        snprintf(cell_line, sizeof cell_line, "%s", lines0[1]);
        if (set.sat) {
            sat_phase(gps_line, cell_line);
        } else {
            printf("survey: satellite off in the settings\n");
        }
        // The transport it had ("-", the default, when none was reported, or
        // when it was "ntn": a survey cut off in its satellite step left that).
        char req[96];
        bool keep = transport[0] && strcmp(transport, "ntn") != 0;
        if (nc_req_transport(req, sizeof req, keep ? transport : "-") > 0) {
            nc_send(req);
        }
        if (nc_time(&epoch, &minutes)) {
            res.local_s = epoch + (int64_t)minutes * 60;
        }
    }

    // The result: on the panel and in /survey.csv.
    if (max17048_read(&b) == ESP_OK) {
        res.batt_mv = b.mv;
        res.batt_pct = b.pct;
    }
    if (res.sd_ok) {
        res.sd_ok = sd_append(&res);
    }
    char lines[SV_LINES][SV_LINE_MAX];
    sv_result_lines(&res, lines);
    if (!have_nc) {
        snprintf(lines[0], SV_LINE_MAX, "Notecard: none");
    }
    show("Survey done - key: again", lines, true);

    if (card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    }
    max17048_stop();  // before the Notecard, whose driver opened the bus
    if (have_nc) {
        notecard_stop();
    }

    if (disp == DISP_OLED) {
        // The OLED shows nothing unpowered: hold the result, then sleep.
        int64_t t0 = esp_timer_get_time();
        while (!key_down() && esp_timer_get_time() - t0 < (int64_t)OLED_HOLD_S * 1000000) {
            led((esp_timer_get_time() - t0) / 500000 % 2 == 0);  // 1 Hz
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (key_down()) {
            restart_mark = RESTART_MAGIC;
            esp_restart();
        }
    }
    oled_off();  // the panel off before LDO2 drops, chosen or not
    if (disp != DISP_OLED) {
        // The picture stays on the e-paper: three blinks say the survey is done.
        for (int i = 0; i < 6; i++) {
            led(i % 2);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    if (epd_ok) {
        CMD(0x10, 0x01);  // the controller's deep sleep; the picture stays
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (epd) {
        spi_bus_remove_device(epd);
    }
    spi_bus_free(SPI2_HOST);
    lines_inputs();  // FR-40.5, before the supply goes off
    supply_ctrl(0);
    if (oled) {
        i2c_master_bus_rm_device(oled);
        oled = NULL;
    }
    if (i2c2) {
        i2c_del_master_bus(i2c2);
    }
    ldo2(false);
    gpio_hold_en(PIN_LDO2);
    gpio_deep_sleep_hold_en();
    sleep_until_key();
}
