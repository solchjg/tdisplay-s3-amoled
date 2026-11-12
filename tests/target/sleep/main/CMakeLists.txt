# The shipped composition root, with the test-only stand-in task below.
# SLEEP_VARIANT -hold also holds the panel's control lines through each sleep.
set(hold_requires "")
set(hold_defs "")
if(SLEEP_VARIANT STREQUAL "-hold")
    set(hold_requires esp_driver_gpio tdisplay_s3_amoled)
    set(hold_defs TDS3_TEST_PIN_HOLD=1)
endif()
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../main/app_main.c"
                       PRIV_REQUIRES device_logic web_log weather history ble_setup ble_settings sampling loop setup_check summary notify lcd identity portal status_http udp_log ota_rx button
                                     mdns_name wifi_net deep_sleep time_sync board rtc_clock nvs_flash
                                     esp_system esp_timer app_update ${hold_requires})
target_compile_definitions(${COMPONENT_LIB} PRIVATE TDS3_TEST_SLEEP_S=${SLEEP_S} TDS3_TEST_HOLD_S=60 ${hold_defs})
