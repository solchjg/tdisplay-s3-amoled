idf_component_register(SRCS "../../oled-probe/main/main.c"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_hw_support)
target_compile_definitions(${COMPONENT_LIB} PRIVATE PROBE_FEATHERS3)
