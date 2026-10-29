idf_component_register(SRCS "lcd.c" "lcd_layout.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES st7789 text tdisplay_s3)
