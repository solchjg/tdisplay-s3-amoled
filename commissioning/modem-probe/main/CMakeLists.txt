idf_component_register(SRCS "main.c" "oled.c" "font6x12.c"
                       PRIV_REQUIRES ntfy_tx text esp_lcd esp_driver_i2c esp_driver_gpio esp_netif
                                     esp_event esp_http_client mbedtls esp_timer)
