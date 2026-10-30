idf_component_register(SRCS "summary.c" "summary_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common mail_tx portal weather
                       PRIV_REQUIRES nvs_flash)
