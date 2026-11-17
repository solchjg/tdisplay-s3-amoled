cmake_minimum_required(VERSION 3.22)

# ws-epaper-probe's program for Waveshare's 2.13" e-Paper HAT (250 x 122) on
# the FeatherS3D's e-paper pins (FSD §40.2), in place of the 4.2" panel.
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/text)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fs3_epd213_probe)
