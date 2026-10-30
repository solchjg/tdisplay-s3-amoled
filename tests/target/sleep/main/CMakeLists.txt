# The shipped composition root, with the test-only stand-in task below.
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../main/app_main.c"
                       PRIV_REQUIRES device_logic web_log lcd identity portal status_http udp_log ota_rx button
                                     mdns_name wifi_net deep_sleep
                                     esp_system esp_timer app_update)
target_compile_definitions(${COMPONENT_LIB} PRIVATE TDS3_TEST_SLEEP_S=${SLEEP_S})
