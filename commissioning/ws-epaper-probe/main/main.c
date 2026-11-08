// ws-epaper-probe: the Waveshare 4.2" e-Paper Module (V2, 400 x 300, SSD1683,
// 8-pin board) on the Waveshare ESP32-S3-SIM7670G-4G (FSD §36, §35.2). Over
// and over, every 30 s: a hardware reset, a full refresh of a frame and six
// text lines, five partial refreshes of a counter on line 6, then the
// controller's power off and deep sleep; each run tries another reset
// timing (see app_main). Each step's BUSY time is printed (`wsepaper:`;
// CHECK marks a result to look at). The modem's supply (GPIO 21) is held
// low, so the USB console never drops. Before each reset it prints the
// BUSY and RST levels and BUSY's voltage (`wsepaper: pins`). Never released.
//
// Wiring (P3 / P2 header labels): VCC 3V3, GND GND, CLK IO7, DIN IO8,
// CS IO3, DC IO10, RST IO20, BUSY IO1.
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "text.h"

#define PIN_SCK 7
#define PIN_MOSI 8
#define PIN_CS 3
#define PIN_DC 10    // low = command
#define PIN_RST 20   // low = reset
#define PIN_BUSY 1   // high = busy
#define PIN_MODEM_SUPPLY 21

#define SPI_HZ 1000000
#define EPD_W 400
#define EPD_H 300
#define EPD_BYTES (EPD_W / 8 * EPD_H)
#define BUSY_LIMIT_MS 10000
#define SPI_CHUNK 4000

static spi_device_handle_t spi;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static uint8_t fb[EPD_BYTES];  // 1 bit per pixel, 1 = white

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

#define CMD(c, ...)                         \
    do {                                    \
        const uint8_t d_[] = {__VA_ARGS__}; \
        epd_cmd((c), d_, sizeof d_);        \
    } while (0)

// BUSY's time high in ms, or -1 past the limit.
static long epd_wait(void)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_BUSY) == 1) {
        if (esp_timer_get_time() - t0 > (int64_t)BUSY_LIMIT_MS * 1000) {
            return -1;
        }
        vTaskDelay(1);  // at least one tick, so IDLE runs (5 ms is 0 ticks at 100 Hz)
    }
    return (long)((esp_timer_get_time() - t0) / 1000);
}

static void busy_input(void)
{
    gpio_config_t in = {.pin_bit_mask = 1ULL << PIN_BUSY, .mode = GPIO_MODE_INPUT};
    gpio_config(&in);
}

// BUSY's logic level, its voltage (GPIO 1 is ADC1 channel 0) and RST read back.
// The board's VBAT/3 divider may also sit on GPIO 1 (R85/R86/R87).
static void pins(const char *when)
{
    int busy = gpio_get_level(PIN_BUSY);
    int rst = gpio_get_level(PIN_RST);
    int raw = -1, mv = -1;
    adc_oneshot_chan_cfg_t cc = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc != NULL && adc_oneshot_config_channel(adc, ADC_CHANNEL_0, &cc) == ESP_OK &&
        adc_oneshot_read(adc, ADC_CHANNEL_0, &raw) == ESP_OK && cali != NULL) {
        adc_cali_raw_to_voltage(cali, raw, &mv);
    }
    busy_input();
    printf("wsepaper: pins %s busy=%d (%d mV) rst=%d\n", when, busy, mv, rst);
}

static void adc_start(void)
{
    adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&uc, &adc) != ESP_OK) {
        adc = NULL;
        return;
    }
    adc_cali_curve_fitting_config_t kc = {.unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
                                          .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&kc, &cali) != ESP_OK) {
        cali = NULL;
    }
}

static esp_err_t bus_start(void)
{
    gpio_set_level(PIN_MODEM_SUPPLY, 0);
    gpio_config_t modem = {.pin_bit_mask = 1ULL << PIN_MODEM_SUPPLY, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&modem);
    gpio_set_level(PIN_RST, 1);
    // INPUT_OUTPUT so RST can be read back (GPIO 20 is the USB D+ pad).
    gpio_config_t out = {.pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST), .mode = GPIO_MODE_INPUT_OUTPUT};
    gpio_config(&out);
    busy_input();
    adc_start();
    spi_bus_config_t bc = {.sclk_io_num = PIN_SCK, .mosi_io_num = PIN_MOSI, .miso_io_num = -1,
                           .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = SPI_CHUNK};
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }
    spi_device_interface_config_t dc = {.clock_speed_hz = SPI_HZ, .mode = 0,
                                        .spics_io_num = PIN_CS, .queue_size = 1};
    return spi_bus_add_device(SPI2_HOST, &dc, &spi);
}

// Hardware reset (which also ends the deep sleep), software reset, setup.
// RST is low for low_us, then high for high_us before the software reset,
// both exact (busy waits, no tick rounding, no pin readings in between).
static long epd_init(int low_us, int high_us)
{
    pins("before");
    gpio_set_level(PIN_RST, 0);
    esp_rom_delay_us(low_us);
    int busy_low = gpio_get_level(PIN_BUSY);
    gpio_set_level(PIN_RST, 1);
    esp_rom_delay_us(high_us);
    printf("wsepaper: reset low %d us (busy=%d), high %d us (busy=%d)\n", low_us, busy_low, high_us,
           gpio_get_level(PIN_BUSY));
    epd_cmd(0x12, NULL, 0);  // software reset: BUSY high for a few ms
    long high_us = 0;
    int64_t t0 = esp_timer_get_time(), seen = -1;
    while (esp_timer_get_time() - t0 < 50000) {  // a tight 50 ms watch
        if (gpio_get_level(PIN_BUSY) == 1) {
            if (seen < 0) {
                seen = esp_timer_get_time() - t0;
            }
            high_us = (long)(esp_timer_get_time() - t0 - seen);
        }
    }
    printf("wsepaper: 0x12 busy high %s (first at %lld us, for %ld us)\n",
           seen < 0 ? "never CHECK" : "seen ok", seen, high_us);
    pins("after-0x12");
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

static long epd_full(void)
{
    epd_write(0x24);
    epd_write(0x26);
    CMD(0x21, 0x40, 0x00);  // red RAM bypassed as 0
    CMD(0x22, 0xF7);
    epd_cmd(0x20, NULL, 0);
    return epd_wait();
}

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

// The analog block and the clock off (0x22 0x83, as Waveshare's and
// GxEPD2's power-off do; a partial refresh's 0xFC leaves them on), then, if
// asked, deep sleep mode 1, which RST ends.
static long epd_sleep(bool deep)
{
    CMD(0x22, 0x83);
    epd_cmd(0x20, NULL, 0);
    long ms = epd_wait();
    if (deep) {
        CMD(0x10, 0x01);
    }
    return ms;
}

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

static void screen(int run, int count)
{
    memset(fb, 0xFF, sizeof fb);
    for (int i = 0; i < EPD_W; i++) {
        px(i, 0, true), px(i, 1, true), px(i, EPD_H - 2, true), px(i, EPD_H - 1, true);
    }
    for (int i = 0; i < EPD_H; i++) {
        px(0, i, true), px(1, i, true), px(EPD_W - 2, i, true), px(EPD_W - 1, i, true);
    }
    char s[48];
    draw(&text_font_small, 8, 12, "Waveshare 4.2in V2 SSD1683");
    draw(&text_font_small, 8, 52, "SIM7670G board, SLOT2");
    snprintf(s, sizeof s, "Run %d", run);
    draw(&text_font_small, 8, 92, s);
    draw(&text_font_small, 8, 132, "Full refresh, then 5");
    draw(&text_font_small, 8, 172, "partial ones below");
    snprintf(s, sizeof s, "Partial %d of 5", count);
    draw(&text_font_large, 8, 230, s);
}

// Each run tries one reset timing (low, high in us), in turn and then over
// again, all ending with the power off and the deep sleep. A refresh that
// hangs is left as it is: the next run's reset shows whether it recovers.
static const int timing[][2] = {
    {10000, 10000}, {1000, 1000}, {2000, 2000}, {5000, 5000}, {2000, 10000},
    {10000, 2000},  {200, 1000},  {20000, 2000}, {2000, 20000},
};
#define TIMINGS (int)(sizeof timing / sizeof timing[0])

void app_main(void)
{
    esp_err_t err = bus_start();
    printf("wsepaper: spi %s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return;
    }
    for (int run = 1;; run++) {
        int low = timing[(run - 1) % TIMINGS][0], high = timing[(run - 1) % TIMINGS][1];
        printf("wsepaper: run %d at %lld s since boot\n", run, esp_timer_get_time() / 1000000);
        long ms = epd_init(low, high);
        printf("wsepaper: run %d reset busy=%ld ms %s\n", run, ms, ms < 0 ? "CHECK (BUSY stuck high or unwired)" : "ok");
        long full = -2;
        int good = 0;
        if (ms >= 0) {
            screen(run, 0);
            full = epd_full();
            printf("wsepaper: full refresh %ld ms %s\n", full, full > 500 && full < 6000 ? "ok" : "CHECK");
            for (int i = 1; i <= 5; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                screen(run, i);
                ms = epd_partial();
                good += ms > 100 && ms < 3000;
                printf("wsepaper: partial %d %ld ms %s\n", i, ms, ms > 100 && ms < 3000 ? "ok" : "CHECK");
            }
            ms = epd_sleep(true);
            printf("wsepaper: power off %ld ms %s; controller asleep; next run in 30 s\n", ms, ms >= 0 ? "ok" : "CHECK");
        }
        printf("wsepaper: RESULT run %d reset %d/%d us: full %ld ms, %d of 5 partials ok -> %s\n", run, low, high, full,
               good, full > 500 && full < 6000 ? "GOOD" : "HUNG");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
