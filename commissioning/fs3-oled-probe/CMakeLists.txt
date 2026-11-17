cmake_minimum_required(VERSION 3.22)

# oled-probe's program for a 0.96" Qwiic OLED on the FeatherS3D's switched
# socket I2C2 (FSD §40.2).
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fs3_oled_probe)
