idf_component_register(SRCS "lcd.c" "lcd_layout.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES rm67162 text tdisplay_s3_amoled esp_driver_spi)
