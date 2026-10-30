idf_component_register(SRCS "setup_check.c" "setup_check_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES nvs_flash esp_driver_gpio esp_timer)
