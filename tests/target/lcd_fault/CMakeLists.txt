# Fault-injection test build (FSD §5.6, seam-lcd-fault-injection): the shipped
# app_main and logic, with the LCD Display Interface replaced by a stub that
# fails on demand. A test artefact; never released.
cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/components/device_logic ${ROOT}/components/web_log
                         ${ROOT}/components/identity ${ROOT}/components/portal
                         ${ROOT}/components/status_http)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver_lcdfault)
