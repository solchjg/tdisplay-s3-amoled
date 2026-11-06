# FR-35.5: the board chosen at build time picks the panel. Requirements are read
# before sdkconfig is known, so both boards' requirements are listed.
if(CONFIG_APP_BOARD_TSIM7080G_S3)
    set(srcs "lcd_none.c" "lcd_layout.c")
else()
    set(srcs "lcd.c" "lcd_layout.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board rm67162 text tdisplay_s3_amoled esp_driver_spi esp_driver_gpio)
