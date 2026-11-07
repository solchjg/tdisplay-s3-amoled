# FSD §35 (FR-35.4, FR-35.19, FR-35.20): the card is on the cellular board
# only; the pure core builds everywhere for the host tier. Requirements are read
# before sdkconfig is known, so the card's are listed for both boards.
set(srcs "settings_card_core.c")
if(CONFIG_APP_BOARD_WS_SIM7670G)
    list(APPEND srcs "settings_card.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board fatfs esp_driver_sdmmc sdmmc vfs)
