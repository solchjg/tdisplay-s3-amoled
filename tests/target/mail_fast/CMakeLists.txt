# Fast mail test build (FSD §25): the shipped main/ and components, with one
# mail_every unit worth 5 minutes instead of an hour, so the summaries of FR-25.3
# come round quickly. A test artefact; never released.
cmake_minimum_required(VERSION 3.22)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${ROOT}/shared/components ${ROOT}/components)
# A T-Display build: the T-SIM7080G modem parts stay out (FR-35.5).
set(EXCLUDE_COMPONENTS sim7080 axp2101 modem_link)
# Distinct from every shipped version, so the bench can tell the boots apart.
set(PROJECT_VER "0.0.0-mail-fast")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_DEFINITIONS "SM_EVERY_UNIT_S=300" APPEND)
project(tdisplay_webserver_mail_fast)
