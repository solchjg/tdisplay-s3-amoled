# The shipped composition root, unmodified.
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../main/app_main.c"
                       PRIV_REQUIRES hello_log hello_logic lcd esp_timer)
