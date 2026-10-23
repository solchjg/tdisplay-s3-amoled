# The shipped composition root, unmodified.
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../main/app_main.c"
                       PRIV_REQUIRES device_logic web_log lcd esp_system)
