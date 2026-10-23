idf_component_register(SRCS "button.c" "button_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_gpio esp_timer web_log)
