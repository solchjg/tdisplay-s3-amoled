idf_component_register(SRCS "status_http.c" "status_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_http_server
                       PRIV_REQUIRES esp_http_server esp_app_format esp_system)
