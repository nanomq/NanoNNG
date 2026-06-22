# NanoNNG Zephyr HTTP Test Demo
#
# Self-contained HTTP server + client test over 127.0.0.1 loopback.
#
# Build: west build -b <board> demo/zephyr_http
#   west build -b qemu_x86   demo/zephyr_http
#   west build -b native_sim demo/zephyr_http

cmake_minimum_required(VERSION 3.20.0)
if(NOT DEFINED ZEPHYR_BASE)
  set(ZEPHYR_BASE $ENV{ZEPHYR_BASE})
endif()
find_package(Zephyr REQUIRED HINTS ${ZEPHYR_BASE})
project(nanonng_http_demo)

target_sources(app PRIVATE main.c)
set(NNG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# HTTP and TCP are already ON by default in nanonng_external.cmake.
# No extra flags needed.

include(../cmake/nanonng_external.cmake)
