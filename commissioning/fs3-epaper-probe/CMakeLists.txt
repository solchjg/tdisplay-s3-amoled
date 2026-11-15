cmake_minimum_required(VERSION 3.22)

# ws-epaper-probe's program with the FeatherS3D's pins (FSD §40.2).
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/text)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fs3_epaper_probe)
