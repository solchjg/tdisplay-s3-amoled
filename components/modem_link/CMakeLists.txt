# The T-SIM7080G build only (FSD §35); the project excludes it from the
# T-Display build, so that build's components and sdkconfig stay as they were.
idf_component_register(SRCS "modem_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES uplink
                       PRIV_REQUIRES axp2101 sim7080 tsim7080g_s3 esp_netif esp_event esp_timer
                                     freertos)
