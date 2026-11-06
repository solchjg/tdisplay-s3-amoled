idf_component_register(SRCS "loop.c" "loop_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common loop420 weather
                       PRIV_REQUIRES ads1115 board esp_driver_gpio esp_rom freertos nvs_flash portal)
