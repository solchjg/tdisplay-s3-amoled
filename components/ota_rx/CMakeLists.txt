idf_component_register(SRCS "ota_rx.c" "ota_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_http_server esp_partition
                       PRIV_REQUIRES esp_http_client app_update)
