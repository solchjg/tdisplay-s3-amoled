// LilyGO T-Display-S3 board support: every number the panel bring-up reads,
// stated here rather than taken from a driver default (FSD §2.2, C-6.1 – C-6.5).
// Plain macros, no ESP-IDF headers, so the host tier compiles it as is and any
// project for this board can reuse it.
#pragma once

#define TDISPLAY_S3_LCD_DATA {39, 40, 41, 42, 45, 46, 47, 48}  // D0 – D7 (C-6.1)
#define TDISPLAY_S3_LCD_WR 8                                    // C-6.2
#define TDISPLAY_S3_LCD_RD 9                                    // held high, never read
#define TDISPLAY_S3_LCD_DC 7
#define TDISPLAY_S3_LCD_CS 6
#define TDISPLAY_S3_LCD_RST 5
#define TDISPLAY_S3_LCD_PWR 15                                  // power enable, high before init (C-6.3)
#define TDISPLAY_S3_LCD_BL 38                                   // backlight, high in SHOWING (FR-6.7)
#define TDISPLAY_S3_LCD_PCLK_HZ 10000000                        // i80 pixel clock (C-6.4)
#define TDISPLAY_S3_LCD_GAP_Y 35                                // 170-px axis offset (C-6.5)

// Visible area in landscape (FR-6.1).
#define TDISPLAY_S3_LCD_W 320
#define TDISPLAY_S3_LCD_H 170
