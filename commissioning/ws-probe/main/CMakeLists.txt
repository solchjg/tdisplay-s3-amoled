idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_driver_i2c esp_driver_sdmmc
                                     sdmmc fatfs esp_hw_support esp_timer spi_flash)
