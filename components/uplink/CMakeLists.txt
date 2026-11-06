idf_component_register(SRCS "uplink.c" "uplink_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common notecard
                       PRIV_REQUIRES board portal nvs_flash)
