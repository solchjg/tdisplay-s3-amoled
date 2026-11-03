cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/ntfy_tx ${ROOT}/shared/components/text)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(modem_probe)
