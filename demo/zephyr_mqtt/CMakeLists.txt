# NanoNNG Zephyr MQTT Client Demo — async state-machine style
#
# Build: west build -b qemu_x86 demo/zephyr_mqtt

cmake_minimum_required(VERSION 3.20.0)
if(NOT DEFINED ZEPHYR_BASE)
  set(ZEPHYR_BASE $ENV{ZEPHYR_BASE})
endif()
find_package(Zephyr REQUIRED HINTS ${ZEPHYR_BASE})
project(nanonng_mqtt_demo)

target_sources(app PRIVATE main.c)
set(NNG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Enable MQTT TCP transport and synchronous DNS (single resolver thread)
set(NNG_EXTRA_ARGS
  -DNNG_TRANSPORT_MQTT_TCP=ON
  -DNNG_RESOLV_CONCURRENCY=1)

include(../cmake/nanonng_external.cmake)
