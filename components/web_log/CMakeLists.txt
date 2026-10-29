idf_component_register(SRCS "web_log.c" "web_log_format.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES log app_update esp_app_format esp_system)
