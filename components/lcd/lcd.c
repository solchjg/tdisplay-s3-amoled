// LCD Display Interface (FSD §6): rows of white-on-black text, composed from
// the reusable parts: the board's pins (tdisplay_s3), the panel driver
// (st7789) and the text renderer (text).

#include "lcd.h"

#include "lcd_layout.h"
#include "st7789.h"
#include "tdisplay_s3.h"
#include "text.h"

#define WHITE 0xFFFF
#define BLACK 0x0000

// Settings confirmed on this board by commissioning (capability lcd-st7789).
static const st7789_config_t panel_cfg = {
    .data_gpio = TDISPLAY_S3_LCD_DATA,
    .wr_gpio = TDISPLAY_S3_LCD_WR,
    .rd_gpio = TDISPLAY_S3_LCD_RD,
    .dc_gpio = TDISPLAY_S3_LCD_DC,
    .cs_gpio = TDISPLAY_S3_LCD_CS,
    .rst_gpio = TDISPLAY_S3_LCD_RST,
    .power_gpio = TDISPLAY_S3_LCD_PWR,
    .backlight_gpio = TDISPLAY_S3_LCD_BL,
    .pclk_hz = TDISPLAY_S3_LCD_PCLK_HZ,
    .width = TDISPLAY_S3_LCD_W,
    .height = TDISPLAY_S3_LCD_H,
    .gap_x = 0,
    .gap_y = TDISPLAY_S3_LCD_GAP_Y,
    .invert = true,
    .swap_xy = true,
    .mirror_x = true,
    .mirror_y = false,
    // One DMA transfer at a time: a full-width band of FONT_H rows (20 KiB).
    .canvas_pixels = TDISPLAY_S3_LCD_W * FONT_H,
};

esp_err_t lcd_draw_line(int line, const char *text)
{
    uint16_t *buf = st7789_canvas(NULL);
    if (buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_rect_t r = lcd_text_line_rect(line);
    text_render(text, st7789_bus_colour(WHITE), st7789_bus_colour(BLACK), buf, r.w);
    return st7789_write(r.x, r.y, r.w, r.h);
}

void lcd_backlight(bool on)
{
    st7789_backlight(on);
}

esp_err_t lcd_start(void)
{
    return st7789_start(&panel_cfg);
}
