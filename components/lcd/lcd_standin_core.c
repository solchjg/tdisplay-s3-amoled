// LCD Display Interface, pure core of the bench stand-in (FSD FR-35.22).
#include "lcd_standin_core.h"

#define MARGIN_X 1  // (128 - 21 * 6) / 2
#define TOP 2       // (64 - 6 * 10) / 2
#define CELL_W 6
#define CELL_H 10

lcd_rect_t lcd_standin_line_rect(int line)
{
    lcd_rect_t r = {MARGIN_X, TOP + line * CELL_H, LCD_STANDIN_COLS * CELL_W, CELL_H};
    return r;
}

// Page addressing on both chips: the SSD1306 is told so (20 02); the SH1106
// has nothing else and would read 0x20 as another command, so it is not sent.
static const uint8_t ssd1306_init[] = {
    0xAE,        // display off
    0xD5, 0x80,  // clock
    0xA8, 0x3F,  // 64 rows
    0xD3, 0x00,  // no display offset
    0x40,        // start line 0
    0x8D, 0x14,  // charge pump on
    0x20, 0x02,  // page addressing
    0xA1,        // column 127 at the left
    0xC8,        // rows scanned bottom up
    0xDA, 0x12,  // COM pins
    0x81, 0x80,  // contrast
    0xD9, 0xF1,  // precharge
    0xDB, 0x40,  // VCOM deselect
    0xA4, 0xA6,  // show RAM, not inverted
};

static const uint8_t sh1106_init[] = {
    0xAE,        // display off
    0xD5, 0x80,  // clock
    0xA8, 0x3F,  // 64 rows
    0xD3, 0x00,  // no display offset
    0x40,        // start line 0
    0xAD, 0x8B,  // DC-DC on
    0xA1,        // column 127 at the left
    0xC8,        // rows scanned bottom up
    0xDA, 0x12,  // COM pins
    0x81, 0x80,  // contrast
    0xD9, 0x22,  // precharge
    0xDB, 0x35,  // VCOM deselect
    0xA4, 0xA6,  // show RAM, not inverted
};

const uint8_t *lcd_standin_init(bool sh1106, size_t *n)
{
    *n = sh1106 ? sizeof sh1106_init : sizeof ssd1306_init;
    return sh1106 ? sh1106_init : ssd1306_init;
}

void lcd_standin_page_at(bool sh1106, int page, uint8_t out[3])
{
    int col = sh1106 ? 2 : 0;
    out[0] = (uint8_t)(0xB0 + (page & 7));
    out[1] = (uint8_t)(col & 0x0F);         // lower column nibble
    out[2] = (uint8_t)(0x10 | (col >> 4));  // upper column nibble
}

unsigned lcd_standin_pack(const uint16_t *px, int w, int h, int x0, int y0,
                          uint8_t frame[LCD_STANDIN_PAGES * LCD_STANDIN_W])
{
    if (px == NULL || w <= 0 || h <= 0 || x0 < 0 || y0 < 0 || x0 + w > LCD_STANDIN_W ||
        y0 + h > LCD_STANDIN_H) {
        return 0;
    }
    unsigned pages = 0;
    for (int y = 0; y < h; y++) {
        int py = y0 + y;
        uint8_t bit = (uint8_t)(1u << (py % 8));
        uint8_t *at = &frame[(py / 8) * LCD_STANDIN_W + x0];
        for (int x = 0; x < w; x++) {
            at[x] = px[y * w + x] ? (uint8_t)(at[x] | bit) : (uint8_t)(at[x] & ~bit);
        }
        pages |= 1u << (py / 8);
    }
    return pages;
}
