# No-network test build (FSD §5.6, FR-5.24, seam-no-network-build): the shipped
# composition root and components, compiled so that a station attempt is never
# started. With stored credentials the image stays in CONNECTING, never reaches
# ONLINE, never confirms itself, and T13 must roll it back. A test artefact;
# never released.
cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/components/device_logic ${ROOT}/components/web_log
                         ${ROOT}/components/identity ${ROOT}/components/portal
                         ${ROOT}/components/status_http ${ROOT}/components/udp_log
                         ${ROOT}/components/ota_rx ${ROOT}/components/button
                         ${ROOT}/components/mdns_name ${ROOT}/components/wifi_net
                         ${ROOT}/components/lcd
                         ${ROOT}/components/st7789 ${ROOT}/components/text
                         ${ROOT}/components/tdisplay_s3 ${ROOT}/components/esp_try)
# Distinct from every shipped version, so the bench can tell the boots apart.
set(PROJECT_VER "0.0.0-no-network")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver_nonetwork)
