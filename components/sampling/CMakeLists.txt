idf_component_register(SRCS "sampling.c" "sampling_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common summary weather
                       PRIV_REQUIRES nvs_flash portal)
