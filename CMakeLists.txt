# The following lines of boilerplate have to be in your project's
# CMakeLists.txt, the minimum version of CMake required, and the
# name of your project.
cmake_minimum_required(VERSION 3.16)

# Set the target before including ESP-IDF
set(SUPPORTED_TARGETS esp32c3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiao_esp32c3_template)
