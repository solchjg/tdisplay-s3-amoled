# The shipped composition root, unchanged (same PRIV_REQUIRES as main/CMakeLists.txt).
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../main/app_main.c"
                       PRIV_REQUIRES device_logic web_log weather history setup_check summary notify lcd identity portal status_http udp_log ota_rx button
                                     mdns_name wifi_net deep_sleep time_sync ble_setup ble_settings sampling loop uplink
                                     esp_system esp_timer app_update)
