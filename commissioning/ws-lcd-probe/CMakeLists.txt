cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/st7789 ${ROOT}/shared/components/esp_try
                         ${ROOT}/shared/components/text)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ws_lcd_probe)
