# The shipped composition root, with the test-only hang of the chosen variant.
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../main/app_main.c"
                       PRIV_REQUIRES device_logic web_log weather setup_check lcd identity portal status_http udp_log ota_rx button
                                     mdns_name wifi_net deep_sleep time_sync
                                     esp_system esp_timer app_update)
string(TOUPPER "${HANG_VARIANT}" variant)
target_compile_definitions(${COMPONENT_LIB} PRIVATE TDS3_TEST_HANG_${variant}=1)
