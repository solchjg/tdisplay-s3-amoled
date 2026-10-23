# Fault-injection test build (FSD §5.5, seam-lcd-fault-injection): the shipped
# app_main and logic, with the LCD Display Interface replaced by a stub that
# fails on demand. A test artefact; never released.
cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/components/hello_logic ${ROOT}/components/hello_log)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver_lcdfault)
