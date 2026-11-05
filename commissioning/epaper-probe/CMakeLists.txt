cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/notecard ${ROOT}/shared/components/bme280
                         ${ROOT}/shared/components/text ${ROOT}/shared/components/esp_try)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(epaper_probe)
