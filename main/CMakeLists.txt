idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES device_logic web_log weather setup_check summary mail_tx lcd identity portal status_http udp_log ota_rx button
                                     mdns_name wifi_net deep_sleep time_sync
                                     esp_system esp_timer app_update)
