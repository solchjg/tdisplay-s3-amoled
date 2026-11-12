idf_component_register(SRCS "main.c" "screen.c"
                       PRIV_REQUIRES notecard sat_day rm67162 tdisplay_s3_amoled text
                                     esp_driver_gpio esp_driver_i2c esp_driver_spi esp_timer)
