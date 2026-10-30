# Sleep test build (FSD §21.3): the shipped composition root and components,
# with a stand-in task that asks for SLEEP_S seconds of deep sleep (10 for the
# timer-wake test, 300 for the key-wake test). A test artefact; never released.
cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/components/device_logic ${ROOT}/components/web_log
                         ${ROOT}/shared/components/identity ${ROOT}/shared/components/portal
                         ${ROOT}/shared/components/status_http ${ROOT}/shared/components/udp_log
                         ${ROOT}/shared/components/ota_rx ${ROOT}/shared/components/button
                         ${ROOT}/shared/components/mdns_name ${ROOT}/shared/components/wifi_net
                         ${ROOT}/shared/components/deep_sleep ${ROOT}/shared/components/time_sync
                         ${ROOT}/components/weather ${ROOT}/shared/components/bme280
                         ${ROOT}/components/setup_check ${ROOT}/components/summary ${ROOT}/shared/components/mail_tx
                         ${ROOT}/components/lcd
                         ${ROOT}/shared/components/rm67162 ${ROOT}/shared/components/text
                         ${ROOT}/shared/components/tdisplay_s3_amoled ${ROOT}/shared/components/esp_try)
# Distinct from every shipped version, so the bench can tell the boots apart.
set(SLEEP_S 10 CACHE STRING "Sleep period the stand-in task asks for, in seconds")
set(PROJECT_VER "0.0.0-sleep-${SLEEP_S}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver_sleep)
