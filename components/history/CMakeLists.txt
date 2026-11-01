idf_component_register(SRCS "history.c" "history_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_http_server weather
                       PRIV_REQUIRES flash_ring nvs_flash esp_system)
