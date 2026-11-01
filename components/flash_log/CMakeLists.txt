idf_component_register(SRCS "flash_log.c" "flash_log_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_http_server weather
                       PRIV_REQUIRES flash_ring esp_system lwip)
