idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES device_logic web_log lcd identity portal status_http udp_log ota_rx button
                                     mdns_name wifi_net
                                     esp_system esp_timer app_update)
