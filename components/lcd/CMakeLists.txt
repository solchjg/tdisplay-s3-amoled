# FR-35.5: the board chosen at build time picks the panel. Requirements are read
# before sdkconfig is known, so both boards' requirements are listed. The
# cellular build draws a window on the OLED or the e-paper (FR-35.2).
set(cell "lcd_cell.c" "lcd_epaper.c" "lcd_epaper_layout.c" "lcd_layout.c" "lcd_oled_layout.c")
if(CONFIG_APP_OLED_STANDIN)
    # FR-35.22: the bench stand-in instead of the SSD1322.
    set(srcs ${cell} "lcd_standin.c" "lcd_standin_core.c")
elseif(CONFIG_APP_BOARD_WS_SIM7670G)
    set(srcs ${cell} "lcd_oled.c")
else()
    set(srcs "lcd.c" "lcd_layout.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board rm67162 ssd1322 ssd1683 text tdisplay_s3_amoled esp_driver_spi esp_driver_gpio esp_driver_i2c esp_timer)
