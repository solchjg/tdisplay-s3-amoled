cmake_minimum_required(VERSION 3.22)

# Reusable components live in the esp-idf-components submodule.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/shared/components)

# FR-35.5: the board choice (CONFIG_APP_BOARD_TSIM7080G_S3) decides whether the
# modem's components are in the build, so the T-Display build keeps its
# components and sdkconfig. The choice is read from the sdkconfig when it
# exists, else from the defaults, before the build itself reads them.
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG ${CMAKE_CURRENT_LIST_DIR}/sdkconfig)
endif()
if(EXISTS ${SDKCONFIG})
    set(board_files ${SDKCONFIG})
elseif(DEFINED SDKCONFIG_DEFAULTS)
    set(board_files ${SDKCONFIG_DEFAULTS})
else()
    set(board_files sdkconfig.defaults)
endif()
set(tsim OFF)
foreach(f ${board_files})
    get_filename_component(f ${f} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    if(EXISTS ${f})
        file(STRINGS ${f} hit REGEX "^CONFIG_APP_BOARD_TSIM7080G_S3=y")
        if(hit)
            set(tsim ON)
        endif()
    endif()
endforeach()
if(NOT tsim)
    set(EXCLUDE_COMPONENTS sim7080 axp2101 modem_link)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tdisplay_webserver)
