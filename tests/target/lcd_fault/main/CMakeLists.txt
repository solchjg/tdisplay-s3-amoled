# The shipped composition root, unmodified.
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../main/app_main.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../../main/net.c"
                       INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../main"
                       PRIV_REQUIRES device_logic web_log lcd identity portal status_http
                                     esp_system esp_timer esp_wifi esp_netif esp_event
                                     nvs_flash app_update)
