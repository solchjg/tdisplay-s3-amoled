idf_component_register(SRCS "udp_log.c" "udp_log_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES log lwip)
