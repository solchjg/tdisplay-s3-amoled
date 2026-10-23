idf_component_register(SRCS "lcd.c" "lcd_text.c" "font16x32.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_lcd esp_driver_gpio)
