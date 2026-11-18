// ws-epaper-probe: the Waveshare 4.2" e-Paper Module (V2, 400 x 300, SSD1683,
// 8-pin board) on the Waveshare ESP32-S3-SIM7670G-4G (FSD §36, §35.2). Over
// and over, every 30 s: a hardware reset, a full refresh of a frame and six
// text lines, five partial refreshes of a counter on line 6, then the
// controller's power off and deep sleep; every other run shows the
// firmware's fonts instead (see app_main). Each step's BUSY time is printed (`wsepaper:`;
// CHECK marks a result to look at). The modem's supply (GPIO 21) is held
// low, so the USB console never drops. Before each reset it prints the
// BUSY and RST levels and BUSY's voltage (`wsepaper: pins`). Never released.
//
// Wiring (P3 / P2 header labels): VCC 3V3, GND GND, CLK IO7, DIN IO8,
// CS IO3, DC IO10, RST IO20, BUSY IO1.
//
// With PROBE_EPD213 (commissioning/fs3-epd213-probe) the panel is Waveshare's
// 2.13" e-Paper HAT instead (250 x 122, an SSD1680-family controller, the
// same 8-pin cable): its RAM is 128 x 250 (122 columns shown) and the probe
// draws in landscape, 250 x 122, turned into that RAM by px(). The init is
// the 4.2"'s with the SSD1680's source range (0x21 B = 0x80); a panel whose
// controller needs a waveform loaded by software (an old V2) stays blank or
// grey on the full refresh, which the run's RESULT cannot see: look at it.
// The HAT tried is the V3 (SSD1680), so this build drives it with
// Waveshare's V3 sequence (see v3_init): its runs alternate between the test
// screen with five partials and the font page, then a "Sleeping" page and
// the controller's deep sleep. The HAT's VCC comes from the display supply
// (the Pololu switch, GPIO 17): on at boot, off with every panel line an
// input before the ESP32 deep-sleeps for 60 s, for the sleep current.
// Before the panel, each run mounts the microSD breakout on the same supply
// and bus (FSD §40.2, FR-40.14: SO GPIO 37, CS GPIO 38, the panel's CS held
// high), prints the card, writes 64 KB to /sd/probe.bin, reads it back and
// compares (`wsepaper: sd`), looks for /settings.txt, and unmounts; with no
// card the mount fails and the run goes on. Wiring adds SO to MI (GPIO 37),
// CS to GPIO 38, the card's 3V to the switch's output, DET unconnected.
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#ifdef PROBE_EPD213
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "text.h"

#ifdef PROBE_FEATHERS3
// The FeatherS3D build's pins (FSD §40.2), built as commissioning/fs3-epaper-probe:
// VCC 3V3.1, GND, CLK SCK (36), DIN MO (35), CS 5, DC 6, RST 7, BUSY 10.
#define PIN_SCK 36
#define PIN_MOSI 35
#define PIN_CS 5
#define PIN_DC 6
#define PIN_RST 7
#define PIN_BUSY 10
#define BUSY_ADC ADC_CHANNEL_9  // GPIO 10 is ADC1 channel 9
#define BOARD_LINE "FeatherS3D, SLOT11"
#else
#define PIN_SCK 7
#define PIN_MOSI 8
#define PIN_CS 3
#define PIN_DC 10    // low = command
#define PIN_RST 20   // low = reset
#define PIN_BUSY 1   // high = busy
#define PIN_MODEM_SUPPLY 21
#define BUSY_ADC ADC_CHANNEL_0  // GPIO 1 is ADC1 channel 0
#define BOARD_LINE "SIM7670G board, SLOT2"
#endif

#define SPI_HZ 1000000
#ifdef PROBE_EPD213
#define EPD_W 128  // the controller's RAM: 128 columns (122 shown) x 250 rows
#define EPD_H 250
#define LW 250     // drawn in landscape
#define LH 122
#define SRC_RANGE 0x80  // 0x21 byte B: sources S8 - S167, as Waveshare's 2.13" code
#define PANEL_LINE "Waveshare 2.13in HAT"
#else
#define EPD_W 400
#define EPD_H 300
#define LW EPD_W
#define LH EPD_H
#define SRC_RANGE 0x00
#define PANEL_LINE "Waveshare 4.2in V2 SSD1683"
#endif
#define EPD_BYTES (EPD_W / 8 * EPD_H)
#define BUSY_LIMIT_MS 10000
#define SPI_CHUNK 4000

// The reset is an exact 2 ms low and 10 ms high: a low of 10 ms or more
// leaves the 4.2" module's refreshes hanging, while 0.2 to 5 ms works (the
// bench sweep). Odd runs show the test screen and five partial refreshes,
// even runs the font page (the 2.13" too, see the top);
// every run ends with the power off and the deep sleep.
#define RESET_LOW_US 2000
#define RESET_HIGH_US 10000

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

// BUSY's logic level, its voltage and RST read back. On the Waveshare board
// its VBAT/3 divider may also sit on GPIO 1 (R85/R86/R87).
static void pins(const char *when)
{
    int busy = gpio_get_level(PIN_BUSY);
    int rst = gpio_get_level(PIN_RST);
    // A BUSY the panel drives reads the same with either pull; an open one follows
    // the pull. Read before the ADC, which leaves the pad analog until busy_input.
    gpio_set_pull_mode(PIN_BUSY, GPIO_PULLUP_ONLY);
    esp_rom_delay_us(100);
    int up = gpio_get_level(PIN_BUSY);
    gpio_set_pull_mode(PIN_BUSY, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(100);
    int down = gpio_get_level(PIN_BUSY);
    gpio_set_pull_mode(PIN_BUSY, GPIO_FLOATING);
    int raw = -1, mv = -1;
    adc_oneshot_chan_cfg_t cc = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc != NULL && adc_oneshot_config_channel(adc, BUSY_ADC, &cc) == ESP_OK &&
        adc_oneshot_read(adc, BUSY_ADC, &raw) == ESP_OK && cali != NULL) {
        adc_cali_raw_to_voltage(cali, raw, &mv);
    }
    busy_input();
    printf("wsepaper: pins %s busy=%d (%d mV) pulled up=%d down=%d%s rst=%d\n", when, busy, mv, up,
           down, up != down ? " (BUSY follows the pull: open, or the panel unpowered) CHECK" : "", rst);
}

static void adc_start(void)
{
    adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&uc, &adc) != ESP_OK) {
        adc = NULL;
        return;
    }
    adc_cali_curve_fitting_config_t kc = {.unit_id = ADC_UNIT_1, .chan = BUSY_ADC,
                                          .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&kc, &cali) != ESP_OK) {
        cali = NULL;
    }
}

#ifdef PROBE_EPD213
#define PIN_SD_MISO 37
#define PIN_SD_CS 38
#define SD_MOUNT "/sd"
#define SD_TEST_BYTES (64 * 1024)


RTC_DATA_ATTR static int rtc_run;  // kept through the ESP32's deep sleep

// Mount, card details, 64 KB written, read back and compared, /settings.txt
// looked for, unmount. The panel's CS is held high so only the card answers.
// The result again in one line before the run's RESULT: the lines above go
// out while the USB console is still coming back after a timer wake.
static char sd_line[160];

static void sd_test(void)
{
    gpio_set_level(PIN_CS, 1);
    gpio_config_t cs = {.pin_bit_mask = 1ULL << PIN_CS, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&cs);
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SPI2_HOST;
    const esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 2,
                                                 .allocation_unit_size = 0};
    sdmmc_card_t *card = NULL;
    // The card's commands and replies (the R7 echo of CMD8 among them).
    esp_log_level_set("sdmmc_sd", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
    esp_log_level_set("sdspi_host", ESP_LOG_DEBUG);
    esp_log_level_set("sdspi_transaction", ESP_LOG_DEBUG);
    // On top of the 100 ms after the supply: none on odd runs, 250 ms on even
    // ones, to find the settle time the card needs after its supply comes on.
    const int settle_ms = (rtc_run + 1) % 2 ? 0 : 250;  // rtc_run + 1 is this run
    vTaskDelay(pdMS_TO_TICKS(settle_ms));
    printf("wsepaper: sd %d ms after the supply\n", 100 + settle_ms);
    esp_err_t err = ESP_FAIL;
    long mount_ms = 0;
    int attempt = 0;
    while (attempt < 3 && err != ESP_OK) {
        attempt++;
        int64_t t0 = esp_timer_get_time();
        err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mc, &card);
        mount_ms = (long)((esp_timer_get_time() - t0) / 1000);
        printf("wsepaper: sd mount attempt %d: %s after %ld ms\n", attempt, esp_err_to_name(err), mount_ms);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    esp_log_level_set("*", ESP_LOG_INFO);
    if (err != ESP_OK) {
        printf("wsepaper: sd mount %s after %ld ms (no card, or no FAT)\n", esp_err_to_name(err), mount_ms);
        snprintf(sd_line, sizeof sd_line, "%d ms after the supply, mount %s after 3 attempts", 100 + settle_ms,
                 esp_err_to_name(err));
        return;
    }
    printf("wsepaper: sd mount ok %ld ms: %s, %s, %llu MB, %d kHz\n", mount_ms, card->cid.name,
           (card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC",
           (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024),
           card->real_freq_khz);
    static uint8_t buf[4096];
    bool ok = true;
    int64_t t0 = esp_timer_get_time();
    FILE *f = fopen(SD_MOUNT "/probe.bin", "w");
    for (int i = 0; f && i < SD_TEST_BYTES / (int)sizeof buf; i++) {
        for (size_t k = 0; k < sizeof buf; k++) {
            buf[k] = (uint8_t)(i * 31 + k * 7 + rtc_run);
        }
        ok = ok && fwrite(buf, 1, sizeof buf, f) == sizeof buf;
    }
    ok = f && fclose(f) == 0 && ok;
    long w_ms = (long)((esp_timer_get_time() - t0) / 1000);
    t0 = esp_timer_get_time();
    bool same = ok;
    f = fopen(SD_MOUNT "/probe.bin", "r");
    for (int i = 0; f && same && i < SD_TEST_BYTES / (int)sizeof buf; i++) {
        same = fread(buf, 1, sizeof buf, f) == sizeof buf;
        for (size_t k = 0; same && k < sizeof buf; k++) {
            same = buf[k] == (uint8_t)(i * 31 + k * 7 + rtc_run);
        }
    }
    same = f && fclose(f) == 0 && same;
    long r_ms = (long)((esp_timer_get_time() - t0) / 1000);
    printf("wsepaper: sd 64 KB write %ld ms %s, read %ld ms %s\n", w_ms, ok ? "ok" : "CHECK", r_ms,
           same ? "ok, same" : "CHECK");
    FILE *s = fopen(SD_MOUNT "/settings.txt", "r");
    printf("wsepaper: sd /settings.txt %s\n", s ? "found" : "none");
    if (s) {
        fclose(s);
    }
    err = esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    printf("wsepaper: sd unmount %s\n", esp_err_to_name(err));
    snprintf(sd_line, sizeof sd_line,
             "%d ms after the supply, mount ok on attempt %d (%ld ms), 64 KB write %ld ms %s, read %ld ms %s, "
             "settings.txt %s",
             100 + settle_ms, attempt, mount_ms, w_ms, ok ? "ok" : "CHECK", r_ms, same ? "same" : "CHECK",
             s ? "found" : "none");
}
#endif

static esp_err_t bus_start(void)
{
#ifdef PIN_MODEM_SUPPLY
    gpio_set_level(PIN_MODEM_SUPPLY, 0);
    gpio_config_t modem = {.pin_bit_mask = 1ULL << PIN_MODEM_SUPPLY, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&modem);
#endif
    gpio_set_level(PIN_RST, 1);
    // INPUT_OUTPUT so RST can be read back (GPIO 20 is the USB D+ pad).
    gpio_config_t out = {.pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST), .mode = GPIO_MODE_INPUT_OUTPUT};
    gpio_config(&out);
    busy_input();
    adc_start();
#ifdef PIN_SD_CS
    const int miso = PIN_SD_MISO;
#else
    const int miso = -1;
#endif
    spi_bus_config_t bc = {.sclk_io_num = PIN_SCK, .mosi_io_num = PIN_MOSI, .miso_io_num = miso,
                           .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = SPI_CHUNK};
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }
#ifdef PIN_SD_CS
    sd_test();  // FR-40.14: the card before any panel traffic
#endif
    spi_device_interface_config_t dc = {.clock_speed_hz = SPI_HZ, .mode = 0,
                                        .spics_io_num = PIN_CS, .queue_size = 1};
    return spi_bus_add_device(SPI2_HOST, &dc, &spi);
}

// Hardware reset (which also ends the deep sleep), software reset, setup.
// RST is low for low_us, then high for rst_high_us before the software reset,
// both exact (busy waits, no tick rounding, no pin readings in between).
static long epd_init(int low_us, int rst_high_us)
{
    pins("before");
    gpio_set_level(PIN_RST, 0);
    esp_rom_delay_us(low_us);
    int busy_low = gpio_get_level(PIN_BUSY);
    gpio_set_level(PIN_RST, 1);
    esp_rom_delay_us(rst_high_us);
    printf("wsepaper: reset low %d us (busy=%d), high %d us (busy=%d)\n", low_us, busy_low, rst_high_us,
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
    CMD(0x21, 0x40, SRC_RANGE);  // red RAM bypassed as 0
    CMD(0x22, 0xF7);
    epd_cmd(0x20, NULL, 0);
    return epd_wait();
}

static long epd_partial(void)
{
    epd_write(0x24);
    CMD(0x21, 0x00, SRC_RANGE);
    CMD(0x22, 0xFC);
    epd_cmd(0x20, NULL, 0);
    long ms = epd_wait();
    epd_write(0x26);  // the base for the next partial refresh
    return ms;
}

#ifdef PROBE_EPD213
// The 2.13" HAT is the V3 (SSD1680; the "V3" sticker on its Rev2.1 board),
// driven as Waveshare's EPD_2in13_V3.c drives it: their init with the
// WS_20_30 waveform loaded by software and 0x22 0xC7 for a full refresh, the
// base picture in both RAMs, each partial as v3_partial makes it, then deep
// sleep (0x10 0x01) with no power off. Each waveform is 153 bytes of LUT, then 0x3F, the gate voltage (0x03), VSH, VSH2, VSL
// (0x04) and VCOM (0x2C).
static const uint8_t v3_lut_full[159] = {
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

static const uint8_t v3_lut_partial[159] = {
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

static void v3_reset(void)
{
    gpio_set_level(PIN_RST, 0);
    esp_rom_delay_us(RESET_LOW_US);
    gpio_set_level(PIN_RST, 1);
    esp_rom_delay_us(RESET_HIGH_US);
}

static long v3_lut(const uint8_t *lut)
{
    epd_cmd(0x32, lut, 153);
    long ms = epd_wait();
    CMD(0x3F, lut[153]);
    CMD(0x03, lut[154]);
    CMD(0x04, lut[155], lut[156], lut[157]);
    CMD(0x2C, lut[158]);
    return ms;
}

static void v3_window(void)
{
    CMD(0x44, 0x00, EPD_W / 8 - 1);
    CMD(0x45, 0x00, 0x00, (EPD_H - 1) & 0xFF, (EPD_H - 1) >> 8);
}

// BUSY's time after the software reset, or -1.
static long v3_init(void)
{
    pins("before");
    v3_reset();
    if (epd_wait() < 0) {
        return -1;
    }
    epd_cmd(0x12, NULL, 0);
    long ms = epd_wait();
    if (ms < 0) {
        return -1;
    }
    CMD(0x01, (EPD_H - 1) & 0xFF, (EPD_H - 1) >> 8, 0x00);
    CMD(0x11, 0x03);
    v3_window();
    CMD(0x3C, 0x05);
    CMD(0x21, 0x00, 0x80);
    CMD(0x18, 0x80);
    if (epd_wait() < 0) {
        return -1;
    }
    return v3_lut(v3_lut_full) < 0 ? -1 : ms;
}

static long v3_full(void)
{
    epd_write(0x24);
    epd_write(0x26);
    CMD(0x22, 0xC7);
    epd_cmd(0x20, NULL, 0);
    return epd_wait();
}

// Each partial as Waveshare's V3 code makes it: a hardware reset (after an
// update the controller takes no new LUT without one), then, once BUSY is
// low again, their partial waveform, RAM ping-pong, border 0x80, the clock
// and analog block on (0x22 0xC0), the B/W RAM and 0x22 0x0F. Without the
// wait after the reset the LUT write was lost and the partial ran the full
// waveform; without the reset each partial hung or ran the full waveform.
static long v3_partial(void)
{
    v3_reset();
    long rst = epd_wait();
    vTaskDelay(pdMS_TO_TICKS(20));
    long lut = rst < 0 ? -1 : v3_lut(v3_lut_partial);
    CMD(0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00);
    CMD(0x3C, 0x80);
    CMD(0x22, 0xC0);
    epd_cmd(0x20, NULL, 0);
    long on = epd_wait();
    printf("wsepaper: partial setup: reset busy %ld ms, LUT busy %ld ms, analog on %ld ms\n", rst, lut, on);
    if (lut < 0 || on < 0) {
        return -1;
    }
    v3_window();
    epd_write(0x24);
    CMD(0x22, 0x0F);
    epd_cmd(0x20, NULL, 0);
    return epd_wait();
}

// Deep sleep mode 1; the SSD1680 holds BUSY high from then on, so it is not waited on.
static void v3_sleep(void)
{
    CMD(0x10, 0x01);
    vTaskDelay(pdMS_TO_TICKS(100));
}
#endif

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

// One pixel at (x, y) of the LW x LH picture, 1 = white in the RAM.
static void px(int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= LW || y >= LH) {
        return;
    }
#ifdef PROBE_EPD213
    // Landscape on the portrait RAM: picture x runs down the RAM's rows and
    // picture y across its 122 shown columns, top of the picture at column 121.
    int rx = LH - 1 - y;
    y = x;
    x = rx;
#endif
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    if (black) {
        fb[y * (EPD_W / 8) + x / 8] &= (uint8_t)~bit;
    } else {
        fb[y * (EPD_W / 8) + x / 8] |= bit;
    }
}

static void draw(const text_font_t *f, int x, int y, const char *s)
{
    static uint16_t line[LW * FONT_H];
    int w = text_font_width(f, s);
    if (w > LW - x) {
        w = LW - x;
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
    for (int i = 0; i < LW; i++) {
        px(i, 0, true), px(i, 1, true), px(i, LH - 2, true), px(i, LH - 1, true);
    }
    for (int i = 0; i < LH; i++) {
        px(0, i, true), px(1, i, true), px(LW - 2, i, true), px(LW - 1, i, true);
    }
    char s[48];
#ifdef PROBE_EPD213
    // A solid 10 x 10 mark in the top-left corner shows the picture's turn.
    for (int y = 4; y < 14; y++) {
        for (int x = 4; x < 14; x++) {
            px(x, y, true);
        }
    }
    draw(&text_font_mini, 20, 4, PANEL_LINE);
    draw(&text_font_mini, 8, 22, BOARD_LINE);
    snprintf(s, sizeof s, "Run %d: full, then 5 partials", run);
    draw(&text_font_tiny, 8, 42, s);
    draw(&text_font_tiny, 8, 54, "Mark top left, frame on all 4 edges");
    snprintf(s, sizeof s, "Partial %d of 5", count);
    draw(&text_font_large, 8, 74, s);
    return;
#endif
    draw(&text_font_small, 8, 12, PANEL_LINE);
    draw(&text_font_small, 8, 52, BOARD_LINE);
    snprintf(s, sizeof s, "Run %d", run);
    draw(&text_font_small, 8, 92, s);
    draw(&text_font_small, 8, 132, "Full refresh, then 5");
    draw(&text_font_small, 8, 172, "partial ones below");
    snprintf(s, sizeof s, "Partial %d of 5", count);
    draw(&text_font_large, 8, 230, s);
}

// The firmware's fonts on the panel, each with sample lines, so their
// sizes can be judged at 4.2".
#ifdef PROBE_EPD213
// The last picture before the ESP32's deep sleep.
static void sleeping_page(int run)
{
    memset(fb, 0xFF, sizeof fb);
    draw(&text_font_large, 8, 30, "Sleeping");
    char s[48];
    snprintf(s, sizeof s, "60 s after run %d", run);
    draw(&text_font_mini, 8, 80, s);
}
#endif

static void font_page(int run)
{
    memset(fb, 0xFF, sizeof fb);
    static const struct {
        const text_font_t *f;
        const char *name;
        const char *lines[2];
    } fonts[] = {
        {&text_font_large, "16x32", {"21.4 C  45 %", "1013 hPa  12.0 mA"}},
        {&text_font_small, "12x24", {"Temp 21.4 C  RH 45 %", "Loop 12.00 mA  Bat 3.92 V"}},
        {&text_font_mini, "8x16", {"Temp 21.4 C  RH 45 %  1013.2 hPa", "Loop 12.00 mA  Bat 3.92 V  LTE -87 dBm"}},
        {&text_font_tiny, "6x10", {"Temp 21.4 C  RH 45 %  1013.2 hPa  Loop 12.00 mA",
                                   "Bat 3.92 V  LTE -87 dBm  last send 21:30  next 22:00"}},
    };
    int y = 4;
    char s[48];
#ifdef PROBE_EPD213
    size_t first = 1;  // 122 rows hold the three smaller fonts
#else
    size_t first = 0;
#endif
    for (size_t i = first; i < sizeof fonts / sizeof fonts[0]; i++) {
        snprintf(s, sizeof s, "%s font:", fonts[i].name);
        draw(&text_font_tiny, 4, y, s);
        y += 12;
        for (int k = 0; k < 2; k++) {
            draw(fonts[i].f, 4, y, fonts[i].lines[k]);
            y += fonts[i].f->h + 2;
        }
        y += 6;
        for (int x = 0; x < LW; x++) {
            px(x, y - 4, true);
        }
    }
    snprintf(s, sizeof s, "Font page, run %d", run);
    draw(&text_font_tiny, 4, LH - 12, s);
}

#ifdef PROBE_EPD213

// The HAT's VCC from the display supply (§40.2, FR-40.4): the Pololu 2808's
// CTRL on GPIO 17 latches, so it is driven high (on) or low (off) for 10 ms
// and then left floating, as lcd_cell.c does. The HAT draws 2.8 mA whenever
// it is powered, its controller asleep or not.
#define PIN_SUPPLY 17

static void supply_ctrl(int level)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << PIN_SUPPLY, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(PIN_SUPPLY, level);
    vTaskDelay(pdMS_TO_TICKS(10));
    c.mode = GPIO_MODE_INPUT;
    gpio_config(&c);
}

// FR-40.5: every panel line an input without pulls, so none feeds the HAT
// through its level shifter, then the supply off.
static void supply_off(void)
{
    spi_bus_remove_device(spi);
    spi_bus_free(SPI2_HOST);
    gpio_config_t in = {.pin_bit_mask = (1ULL << PIN_SCK) | (1ULL << PIN_MOSI) | (1ULL << PIN_CS) |
                                        (1ULL << PIN_DC) | (1ULL << PIN_RST) |
                                        (1ULL << PIN_SD_MISO) | (1ULL << PIN_SD_CS),
                        .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&in);
    supply_ctrl(0);
}
#endif

void app_main(void)
{
#ifdef PROBE_EPD213
    supply_ctrl(1);  // the HAT on before any line drives it (FR-40.8: 100 ms)
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
    esp_err_t err = bus_start();
    printf("wsepaper: spi %s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return;
    }
#ifdef PROBE_EPD213
    for (int run = ++rtc_run;;) {
        bool fonts = run % 2 == 0;
        printf("wsepaper: run %d (%s)\n", run, fonts ? "font page" : "test screen");
#else
    for (int run = 1;; run++) {
        bool fonts = run % 2 == 0;
        printf("wsepaper: run %d at %lld s since boot (%s)\n", run, esp_timer_get_time() / 1000000,
               fonts ? "font page" : "test screen");
#endif
#ifdef PROBE_EPD213
        long ms = v3_init();
#else
        long ms = epd_init(RESET_LOW_US, RESET_HIGH_US);
#endif
        printf("wsepaper: run %d reset busy=%ld ms %s\n", run, ms, ms < 0 ? "CHECK (BUSY stuck high or unwired)" : "ok");
        long full = -2;
        int good = 0;
        if (ms >= 0) {
            if (fonts) {
                font_page(run);
            } else {
                screen(run, 0);
            }
#ifdef PROBE_EPD213
            full = v3_full();
#else
            full = epd_full();
#endif
            printf("wsepaper: full refresh %ld ms %s\n", full, full > 500 && full < 6000 ? "ok" : "CHECK");
            for (int i = 1; !fonts && i <= 5; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                screen(run, i);
#ifdef PROBE_EPD213
                ms = v3_partial();
#else
                ms = epd_partial();
#endif
                good += ms > 100 && ms < 3000;
                printf("wsepaper: partial %d %ld ms %s\n", i, ms, ms > 100 && ms < 3000 ? "ok" : "CHECK");
            }
#ifdef PROBE_EPD213
            if (!fonts) {
                printf("wsepaper: partials done; sleeping page in 5 s\n");
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
            // A fresh init and a full refresh for the "Sleeping" page, so it
            // shows whatever state the partials left the controller in.
            ms = v3_init();
            sleeping_page(run);
            long slp = ms < 0 ? -1 : v3_full();
            printf("wsepaper: sleeping page %ld ms %s\n", slp, slp > 500 ? "ok" : "CHECK");
            v3_sleep();
            printf("wsepaper: controller asleep\n");
#else
            ms = epd_sleep(true);
            printf("wsepaper: power off %ld ms %s; controller asleep\n", ms, ms >= 0 ? "ok" : "CHECK");
#endif
        }
        printf("wsepaper: RESULT run %d (%s): full %ld ms, %d of %d partials ok -> %s\n", run,
               fonts ? "font page" : "test screen", full, good, fonts ? 0 : 5,
               full > 500 && full < 6000 && (fonts || good == 5) ? "GOOD" : "CHECK");
#ifdef PROBE_EPD213
        printf("wsepaper: sd summary run %d: %s\n", run, sd_line);
        supply_off();
        printf("wsepaper: panel lines inputs, display supply off\n");
        printf("wsepaper: sleep 60 s\n");
        fflush(stdout);
        esp_sleep_enable_timer_wakeup(60ULL * 1000000);
        esp_deep_sleep_start();
#else
        vTaskDelay(pdMS_TO_TICKS(30000));
#endif
    }
}
