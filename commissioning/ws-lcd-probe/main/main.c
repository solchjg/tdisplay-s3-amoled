// ws-lcd-probe: the Waveshare 1.9" LCD module (170 x 320, ST7789, SPI) on the
// Waveshare ESP32-S3-SIM7670G-4G, through the shared st7789 driver's SPI mode.
// It shares SPI2 (SCK, MOSI, DC) with the OLED of §35.2, whose CS is held
// high, and by default takes the e-paper's lines of §36 (CS, RST, BUSY as BL),
// the e-paper unplugged; menuconfig moves CS, RST and BL. Over and over: five full-screen fills (red, green, blue, white,
// black) with their times, a frame with three corner marks and two text
// lines, eight colour bars, five seconds of a moving box with its frame
// rate, and the display off and on. Each step prints a `wslcd:` line; CHECK
// marks what to look at on the panel. The modem's supply (GPIO 21) is held
// low, so the USB console never drops. Never released.
//
// Wiring (P3 / P2 header labels), the e-paper's cable: VCC 3V3, GND GND,
// DIN IO8, CLK IO7, CS IO3, DC IO10, RST IO20, BL IO1. RST or BL at -1 in
// menuconfig means that LCD pin is tied to 3V3 instead (software reset,
// backlight always on).
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st7789.h"
#include "text.h"

#define PIN_SCK 7
#define PIN_MOSI 8
#define PIN_CS CONFIG_PROBE_PIN_CS
#define PIN_DC 10
#define PIN_RST CONFIG_PROBE_PIN_RST
#define PIN_BL CONFIG_PROBE_PIN_BL
#define PIN_OLED_CS 9
#define PIN_MODEM_SUPPLY 21

#if CONFIG_PROBE_LANDSCAPE
#define LCD_W 320
#define LCD_H 170
#else
#define LCD_W 170
#define LCD_H 320
#endif
#define CANVAS_PIXELS (320 * 32)  // one 16 x 32 text line across the long side
#define MARK 20
#define BOX 40
#define MOTION_MS 5000

#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define YELLOW 0xFFE0
#define CYAN 0x07FF
#define MAGENTA 0xF81F

static void out_pin(int pin, int level)
{
    gpio_set_level(pin, level);
    gpio_config_t io = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
}

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void hold(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// One line of text, w pixels wide, at (x, y): rendered into the canvas in host
// order, then turned into bus order in place.
static esp_err_t line(int x, int y, int w, const char *s, uint16_t fg, uint16_t bg)
{
    const text_font_t *f = w >= 16 * 16 ? &text_font_large : &text_font_small;
    size_t pixels;
    uint16_t *c = st7789_canvas(&pixels);
    if ((size_t)w * (size_t)f->h > pixels) {
        return ESP_ERR_INVALID_SIZE;
    }
    text_font_render(f, s, fg, bg, c, w);
    for (size_t i = 0; i < (size_t)w * (size_t)f->h; i++) {
        c[i] = st7789_bus_colour(c[i]);
    }
    return st7789_write(x, y, w, f->h);
}

static void fills(void)
{
    static const struct {
        const char *name;
        uint16_t c;
    } steps[] = {{"red", RED}, {"green", GREEN}, {"blue", BLUE}, {"white", WHITE},
                 {"black", BLACK}};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        int64_t t = now_us();
        esp_err_t err = st7789_fill(0, 0, LCD_W, LCD_H, steps[i].c);
        int64_t us = now_us() - t;
        printf("wslcd: fill %s %s ms=%lld kB/s=%lld\n", steps[i].name, esp_err_to_name(err),
               us / 1000, us > 0 ? (int64_t)LCD_W * LCD_H * 2 * 1000 / us : 0);
        hold(1000);
    }
    printf("wslcd: CHECK each fill the colour named, edge to edge: white showing black "
           "means invert is wrong, red and blue swapped means the colour order\n");
}

static void frame(int round)
{
    esp_err_t err = st7789_fill(0, 0, LCD_W, LCD_H, BLACK);
    if (err == ESP_OK) err = st7789_fill(0, 0, LCD_W, 1, WHITE);
    if (err == ESP_OK) err = st7789_fill(0, LCD_H - 1, LCD_W, 1, WHITE);
    if (err == ESP_OK) err = st7789_fill(0, 0, 1, LCD_H, WHITE);
    if (err == ESP_OK) err = st7789_fill(LCD_W - 1, 0, 1, LCD_H, WHITE);
    if (err == ESP_OK) err = st7789_fill(2, 2, MARK, MARK, RED);
    if (err == ESP_OK) err = st7789_fill(LCD_W - 2 - MARK, 2, MARK, MARK, GREEN);
    if (err == ESP_OK) err = st7789_fill(2, LCD_H - 2 - MARK, MARK, MARK, BLUE);
    char s[40];
    snprintf(s, sizeof s, "%dx%d", LCD_W, LCD_H);
    if (err == ESP_OK) err = line(MARK + 4, MARK + 8, LCD_W - 2 * MARK - 8, s, WHITE, BLACK);
    snprintf(s, sizeof s, "r%d %dMHz", round, CONFIG_PROBE_SPI_MHZ);
    if (err == ESP_OK) err = line(MARK + 4, LCD_H / 2 + 8, LCD_W - 2 * MARK - 8, s, YELLOW, BLACK);
    printf("wslcd: frame %s\n", esp_err_to_name(err));
    printf("wslcd: CHECK a 1-pixel white frame on all four edges (a missing edge or a "
           "noise stripe means gap_x/gap_y are wrong); red mark top left, green top "
           "right, blue bottom left (else the mirror settings); the text upright\n");
    hold(5000);
}

static void bars(void)
{
    static const uint16_t c[] = {WHITE, YELLOW, CYAN, GREEN, MAGENTA, RED, BLUE, BLACK};
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 8 && err == ESP_OK; i++) {
        int x0 = LCD_W * i / 8, x1 = LCD_W * (i + 1) / 8;
        err = st7789_fill(x0, 0, x1 - x0, LCD_H, c[i]);
    }
    printf("wslcd: bars %s\n", esp_err_to_name(err));
    printf("wslcd: CHECK eight bars left to right: white yellow cyan green magenta red "
           "blue black\n");
    hold(3000);
}

static void motion(void)
{
    esp_err_t err = st7789_fill(0, 0, LCD_W, LCD_H, BLACK);
    int x = 0, y = 0, dx = 3, dy = 2, frames = 0;
    int64_t start = now_us();
    while (err == ESP_OK && now_us() - start < MOTION_MS * 1000LL) {
        int nx = x + dx, ny = y + dy;
        if (nx < 0 || nx > LCD_W - BOX) {
            dx = -dx;
            nx = x + dx;
        }
        if (ny < 0 || ny > LCD_H - BOX) {
            dy = -dy;
            ny = y + dy;
        }
        err = st7789_fill(x, y, BOX, BOX, BLACK);
        if (err == ESP_OK) err = st7789_fill(nx, ny, BOX, BOX, CYAN);
        x = nx;
        y = ny;
        frames++;
        vTaskDelay(1);  // let the idle task feed the watchdog
    }
    int64_t ms = (now_us() - start) / 1000;
    printf("wslcd: motion %s frames=%d ms=%lld fps=%lld\n", esp_err_to_name(err), frames, ms,
           ms > 0 ? frames * 1000LL / ms : 0);
    printf("wslcd: CHECK a cyan box bouncing off the edges, no trail\n");
}

static void display_off_on(void)
{
    esp_err_t off = st7789_display(false);
    hold(1500);
    esp_err_t on = st7789_display(true);
    printf("wslcd: display off %s on %s\n", esp_err_to_name(off), esp_err_to_name(on));
    printf("wslcd: CHECK the picture went dark (backlight still lit) and came back\n");
    hold(1500);
}

void app_main(void)
{
    out_pin(PIN_MODEM_SUPPLY, 0);
    out_pin(PIN_OLED_CS, 1);

    const st7789_config_t cfg = {
        .bus = ST7789_BUS_SPI,
        .spi_host = SPI2_HOST,
        .sck_gpio = PIN_SCK,
        .mosi_gpio = PIN_MOSI,
        .cs_gpio = PIN_CS,
        .dc_gpio = PIN_DC,
        .rst_gpio = PIN_RST,
        .power_gpio = -1,
        .backlight_gpio = PIN_BL,
        .pclk_hz = CONFIG_PROBE_SPI_MHZ * 1000000,
        .width = LCD_W,
        .height = LCD_H,
#if CONFIG_PROBE_LANDSCAPE
        .gap_x = 0,
        .gap_y = 35,
        .swap_xy = true,
        .mirror_x = true,
#else
        .gap_x = 35,
        .gap_y = 0,
#endif
        .invert = true,
        .canvas_pixels = CANVAS_PIXELS,
    };
    int64_t t = now_us();
    esp_err_t err = st7789_start(&cfg);
    printf("wslcd: start %s ms=%lld %dx%d spi=%d MHz sck=%d mosi=%d cs=%d dc=%d rst=%d bl=%d\n",
           esp_err_to_name(err), (now_us() - t) / 1000, LCD_W, LCD_H, CONFIG_PROBE_SPI_MHZ,
           PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST, PIN_BL);
    if (err != ESP_OK) {
        printf("wslcd: CHECK the start failed; nothing more runs\n");
        return;
    }
    st7789_backlight(true);
    for (int round = 1;; round++) {
        printf("wslcd: round %d\n", round);
        fills();
        frame(round);
        bars();
        motion();
        display_off_on();
    }
}
