idf_component_register(SRCS "app_main.c" "net.c"
                       PRIV_REQUIRES device_logic web_log lcd identity portal status_http udp_log
                                     esp_system esp_timer esp_wifi esp_netif esp_event
                                     nvs_flash app_update)
