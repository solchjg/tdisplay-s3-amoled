cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/axp2101 ${ROOT}/shared/components/ssd1322
                         ${ROOT}/shared/components/text ${ROOT}/shared/components/tsim7080g_s3
                         ${ROOT}/shared/components/esp_try)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tsim_probe)
