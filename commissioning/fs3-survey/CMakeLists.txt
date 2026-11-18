cmake_minimum_required(VERSION 3.22)

# A site survey on the FeatherS3D (FSD §40.2 wiring): GPS and cellular through
# the Notecard, the battery, the result on the 2.13" e-paper or the 1.3" OLED,
# and a line in /survey.csv on the microSD. A demo program; never released.
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/notecard ${ROOT}/shared/components/max17048
    ${ROOT}/shared/components/text ${ROOT}/shared/components/esp_try)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fs3_survey)
