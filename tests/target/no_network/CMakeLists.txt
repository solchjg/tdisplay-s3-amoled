# No-network test build (FSD §5.6, FR-5.24, seam-no-network-build): the shipped
# composition root and components, compiled so that a station attempt is never
# started. With stored credentials the image stays in CONNECTING, never reaches
# ONLINE, never confirms itself, and T13 must roll it back. A test artefact;
# never released.
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
set(PROJECT_VER "0.0.0-no-network")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver_nonetwork)
