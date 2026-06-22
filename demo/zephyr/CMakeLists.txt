# NanoNNG Zephyr Demo — inproc PAIRv0 test
#
# Build: west build -b <board> demo/zephyr
#   west build -b qemu_x86   demo/zephyr
#   west build -b native_sim demo/zephyr

cmake_minimum_required(VERSION 3.20.0)
if(NOT DEFINED ZEPHYR_BASE)
  set(ZEPHYR_BASE $ENV{ZEPHYR_BASE})
endif()
find_package(Zephyr REQUIRED HINTS ${ZEPHYR_BASE})
project(nanonng_demo)

target_sources(app PRIVATE main.c)
set(NNG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(../cmake/nanonng_external.cmake)
