// The cellular build's e-paper (FSD §36, FR-35.15, FR-35.24): Waveshare's 4.2"
// e-Paper Module (V2, SSD1683) on SPI2 beside the OLED, its own CS, BUSY and
// RST (§35.2). Lines are drawn into the driver's canvas; lcd_flush sends them
// with a full refresh first, then partial ones at most every 5 s; the sleep
// screen is a last full refresh with the status block, then the controller's
// deep sleep.
#include "lcd_panels.h"

#include "board.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "lcd_layout.h"
#include "ssd1683.h"
#include "text.h"

#define FINAL_WAIT_MS 3000  // a partial refresh still running (about 1 s)

static const ssd1683_config_t panel_cfg = {
    .spi_host = SPI2_HOST,
    .sck_gpio = BOARD_OLED_SCK,
    .mosi_gpio = BOARD_OLED_MOSI,
    .cs_gpio = BOARD_EPD_CS,
    .dc_gpio = BOARD_OLED_DC,
    .rst_gpio = BOARD_EPD_RST,
    .busy_gpio = BOARD_EPD_BUSY,
    .pclk_hz = 4 * 1000 * 1000,
    .width = LCD_EPD_W,
    .height = LCD_EPD_H,
};

static bool started, full_due, dirty;
static int64_t last_us;  // the start of the last refresh

esp_err_t epaper_start(void)
{
    esp_err_t err = ssd1683_start(&panel_cfg);
    started = err == ESP_OK;
    full_due = started;  // FR-35.15: a window starts with a full refresh
    dirty = false;
    return err;
}

esp_err_t epaper_draw_line(int line, const char *text)
{
    uint8_t *canvas = ssd1683_canvas(NULL);
    if (!started || canvas == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_epaper_draw(canvas, lcd_epaper_line_rect(line), lcd_epaper_line_font(line), text);
    dirty = true;
    return ESP_OK;
}

esp_err_t epaper_flush(void)
{
    if (!started) {
        return ESP_OK;
    }
    int64_t now = esp_timer_get_time();
    lcd_epd_refresh_t r = lcd_epaper_refresh(full_due, dirty, ssd1683_busy(),
                                             (now - last_us) / 1000);
    if (r == LCD_EPD_NONE) {
        return ESP_OK;
    }
    esp_err_t err = ssd1683_show(r == LCD_EPD_FULL);
    last_us = now;
    full_due = dirty = false;
    return err;
}

esp_err_t epaper_final(const char *const lines[], const char *const rows[], int n)
{
    uint8_t *canvas = ssd1683_canvas(NULL);
    if (!started || canvas == NULL) {
        return ESP_ERR_INVALID_STATE;  // a timer wake never draws (FR-21.13)
    }
    for (int i = 0; i < LCD_TEXT_LINES; i++) {
        lcd_epaper_draw(canvas, lcd_epaper_line_rect(i), lcd_epaper_line_font(i), lines[i]);
    }
    for (int i = 0; i < LCD_EPD_STATUS_ROWS; i++) {
        lcd_epaper_draw(canvas, lcd_epaper_status_rect(i), &text_font_mini, i < n ? rows[i] : "");
    }
    esp_err_t err = ssd1683_wait(FINAL_WAIT_MS);
    if (err == ESP_OK) {
        err = ssd1683_show(true);
    }
    full_due = dirty = false;
    return err;
}

esp_err_t epaper_sleep(void)
{
    if (!started) {
        return ESP_OK;
    }
    started = false;
    return ssd1683_sleep();  // after the refresh still running
}
