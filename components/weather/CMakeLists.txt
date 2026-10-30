idf_component_register(SRCS "weather.c" "weather_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_http_server
                       PRIV_REQUIRES bme280 time_sync esp_system)
