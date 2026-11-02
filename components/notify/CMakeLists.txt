idf_component_register(SRCS "notify.c" "notify_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common ntfy_tx weather
                       PRIV_REQUIRES portal nvs_flash freertos esp_system esp_timer esp_hw_support)
