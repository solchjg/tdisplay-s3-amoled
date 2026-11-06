# FR-35.5: the board chosen at build time picks the panel.
if(CONFIG_APP_BOARD_TSIM7080G_S3)
    idf_component_register(SRCS "lcd_none.c" "lcd_layout.c"
                           INCLUDE_DIRS "include"
                           PRIV_REQUIRES board text tdisplay_s3_amoled esp_driver_gpio)
else()
    idf_component_register(SRCS "lcd.c" "lcd_layout.c"
                           INCLUDE_DIRS "include"
                           PRIV_REQUIRES rm67162 text tdisplay_s3_amoled esp_driver_spi esp_driver_gpio)
endif()
