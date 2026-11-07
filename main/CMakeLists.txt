idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES device_logic web_log weather history setup_check summary notify lcd identity portal status_http udp_log ota_rx button
                                     mdns_name wifi_net deep_sleep time_sync ble_setup ble_settings sampling loop uplink board
                                     esp_system esp_timer app_update)

# FSD §35: the modem link is in the cellular build only (see the project's
# CMakeLists), so it is linked here once the board choice is known.
if(CONFIG_APP_BOARD_WS_SIM7670G)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::modem_link idf::settings_card)
endif()
