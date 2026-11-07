// LCD Display Interface on the cellular build (FSD §35, FR-35.16): the six
// lines on the 3.12" SSD1322 OLED, white on black in the 6 x 10 font, composed
// from the reusable parts: the panel driver (ssd1322) and the text renderer
// (text). lcd_cell.c chooses it or the e-paper for a window (FR-35.2).
#include "lcd_panels.h"

#include "board.h"
#include "driver/spi_master.h"
#include "lcd_layout.h"
#include "ssd1322.h"
#include "text.h"

#define WHITE 0x0F  // the brightest of the 16 grey levels
#define BLACK 0x00

static const ssd1322_config_t panel_cfg = {
    .spi_host = SPI2_HOST,
    .sck_gpio = BOARD_OLED_SCK,
    .mosi_gpio = BOARD_OLED_MOSI,
    .cs_gpio = BOARD_OLED_CS,
    .dc_gpio = BOARD_OLED_DC,
    .rst_gpio = BOARD_OLED_RST,
    .pclk_hz = 10 * 1000 * 1000,
    .width = LCD_OLED_W,
    .height = LCD_OLED_H,
    .col_offset = 28,  // a 256-wide panel on the controller's 480 columns
};

static uint16_t line_px[LCD_OLED_W * FONT_TINY_H];
static bool started;

esp_err_t oled_start(void)
{
    esp_err_t err = ssd1322_start(&panel_cfg);
    started = err == ESP_OK;
    return err;
}

esp_err_t oled_draw_line(int line, const char *text)
{
    uint8_t *canvas = ssd1322_canvas(NULL);
    if (!started || canvas == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_rect_t r = lcd_oled_line_rect(line);
    text_font_render(&text_font_tiny, text, WHITE, BLACK, line_px, r.w);  // cut at 42 characters
    if (ssd1322_pack(line_px, r.w, r.h, r.x, LCD_OLED_W, canvas) == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ssd1322_write(r.y, r.h);
}

void oled_display(bool on)
{
    if (started) {
        (void)ssd1322_display(on);
    }
}

// FR-35.16: the OLED's display off at the end of every window; a timer wake
// never starts it, so it stays off.
esp_err_t oled_sleep(void)
{
    return started ? ssd1322_display(false) : ESP_OK;
}
