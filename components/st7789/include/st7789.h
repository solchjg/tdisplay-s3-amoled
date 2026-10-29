// ST7789 panel on an 8-bit Intel-8080 (i80) bus: bring-up, a DMA canvas and
// rectangle writes. Board-agnostic: every pin and panel setting comes from
// st7789_config_t. One panel per firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries or aborts (FSD §6.5): what
// happens on failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int data_gpio[8];    // D0 – D7
    int wr_gpio;
    int rd_gpio;         // driven high and never read; -1 if not wired
    int dc_gpio;
    int cs_gpio;
    int rst_gpio;
    int power_gpio;      // driven high before init; -1 if none
    int backlight_gpio;  // driven low at start, see st7789_backlight; -1 if none
    uint32_t pclk_hz;
    int width, height;   // visible area after swap_xy
    int gap_x, gap_y;    // visible-area offset in controller RAM
    bool invert;
    bool swap_xy, mirror_x, mirror_y;
    size_t canvas_pixels;  // DMA canvas size; one write sends at most this many
} st7789_config_t;

// Power, bus and panel bring-up, then clear to black. The backlight stays off.
esp_err_t st7789_start(const st7789_config_t *cfg);

// The DMA-capable pixel buffer that st7789_write sends, and its size in
// pixels. NULL before a successful start's allocation. Fill it in bus order
// (st7789_bus_colour); it may be refilled as soon as st7789_write returns.
uint16_t *st7789_canvas(size_t *pixels);

// Send the first w*h canvas pixels to the w x h rectangle at (x, y) and wait
// until DMA has finished reading them. ESP_ERR_TIMEOUT if DMA does not finish.
esp_err_t st7789_write(int x, int y, int w, int h);

// Fill a rectangle of any size with one colour (RGB565, host order).
esp_err_t st7789_fill(int x, int y, int w, int h, uint16_t rgb565);

void st7789_backlight(bool on);

// The 8-bit bus sends bytes in memory order; the ST7789 wants RGB565 MSB first.
static inline uint16_t st7789_bus_colour(uint16_t rgb565)
{
    return (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
}
