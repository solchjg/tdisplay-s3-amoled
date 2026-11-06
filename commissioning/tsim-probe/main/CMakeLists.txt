idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES axp2101 ssd1322 text tsim7080g_s3 esp_driver_gpio
                                     esp_driver_sdmmc sdmmc fatfs esp_hw_support esp_timer)
