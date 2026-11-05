idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES notecard bme280 text esp_driver_i2c esp_driver_spi
                                     esp_driver_gpio esp_hw_support esp_timer)
