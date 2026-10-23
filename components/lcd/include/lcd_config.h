// LCD Display Interface configuration: every number the panel bring-up reads,
// stated here rather than taken from a driver default (FSD §2.2, C-6.1 – C-6.5).
#pragma once

#define LCD_PIN_DATA {39, 40, 41, 42, 45, 46, 47, 48}  // D0 – D7 (C-6.1)
#define LCD_PIN_WR 8                                    // C-6.2
#define LCD_PIN_RD 9                                    // held high, never read
#define LCD_PIN_DC 7
#define LCD_PIN_CS 6
#define LCD_PIN_RST 5
#define LCD_PIN_PWR 15                                  // power enable, high before init (C-6.3)
#define LCD_PIN_BL 38                                   // backlight, high in SHOWING (FR-6.7)
#define LCD_PCLK_HZ 10000000                            // i80 pixel clock (C-6.4)
#define LCD_GAP_Y 35                                    // 170-px axis offset (C-6.5)
