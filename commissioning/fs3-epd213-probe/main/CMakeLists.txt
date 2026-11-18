idf_component_register(SRCS "../../ws-epaper-probe/main/main.c"
                       PRIV_REQUIRES text esp_driver_spi esp_driver_gpio esp_timer esp_adc
                                     fatfs sdmmc esp_driver_sdspi vfs)
target_compile_definitions(${COMPONENT_LIB} PRIVATE PROBE_FEATHERS3 PROBE_EPD213)
