cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components/ntfy_tx ${ROOT}/shared/components/text)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# lwIP's per-interface octet counters (MIB2), read to count the bytes each run
# moves over PPP. Needs CONFIG_LWIP_STATS; set for every component so the
# netif struct has the same layout everywhere.
idf_build_set_property(COMPILE_DEFINITIONS "MIB2_STATS=1" APPEND)
project(modem_probe)
