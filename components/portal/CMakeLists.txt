idf_component_register(SRCS "portal.c" "portal_core.c" "captive_dns.c" "dns_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_http_server log lwip freertos)
