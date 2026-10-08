# GlideKVM Bridge firmware, built with ESP-IDF 5.4:
#   idf.py set-target esp32s3 && idf.py build
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(glidekvm-bridge)
