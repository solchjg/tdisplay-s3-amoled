# Fault-injection test build (FSD §5.6, seam-lcd-fault-injection): the shipped
# app_main and logic, with the LCD Display Interface replaced by a stub that
# fails on demand. A test artefact; never released.
cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/components/device_logic ${ROOT}/components/web_log
                         ${ROOT}/shared/components/identity ${ROOT}/shared/components/portal
                         ${ROOT}/shared/components/status_http ${ROOT}/shared/components/udp_log
                         ${ROOT}/shared/components/ota_rx ${ROOT}/shared/components/button
                         ${ROOT}/shared/components/mdns_name ${ROOT}/shared/components/wifi_net
                         ${ROOT}/shared/components/deep_sleep
                         ${ROOT}/shared/components/esp_try)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver_lcdfault)
