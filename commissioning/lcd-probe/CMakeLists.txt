cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/rm67162 ${ROOT}/shared/components/tdisplay_s3_amoled
                         ${ROOT}/shared/components/esp_try)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lcd_probe)
