idf_component_register(SRCS "flash_log.c" "flash_log_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_http_server weather
                       PRIV_REQUIRES esp_partition nvs_flash freertos esp_system lwip)
