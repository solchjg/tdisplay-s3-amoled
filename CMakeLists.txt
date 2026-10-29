cmake_minimum_required(VERSION 3.22)

# Reusable components live in the esp-idf-components submodule.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/shared/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver)
