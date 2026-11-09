idf_component_register(SRCS "rtc_clock.c" "rtc_clock_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES board rv8803 time_sync nvs_flash esp_timer esp_rom freertos)
