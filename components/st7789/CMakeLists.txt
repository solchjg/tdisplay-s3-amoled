idf_component_register(SRCS "st7789.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_lcd esp_driver_gpio esp_try)
