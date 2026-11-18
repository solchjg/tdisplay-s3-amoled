idf_component_register(SRCS "main.c" "survey_core.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES notecard max17048 bme280 ble_setup nvs_flash text esp_driver_spi esp_driver_gpio
                                     esp_driver_i2c esp_timer fatfs sdmmc esp_driver_sdspi vfs)
