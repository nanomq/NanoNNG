# NanoNNG Zephyr MQTT Client Demo — ExternalProject build
#
# Build: west build -b qemu_x86 .
#
# A minimal MQTT client over TCP using NanoNNG's MQTT protocol.
# Connects to a broker, subscribes to topics, publishes and receives.

cmake_minimum_required(VERSION 3.20.0)
if(NOT DEFINED ZEPHYR_BASE)
  set(ZEPHYR_BASE $ENV{ZEPHYR_BASE})
endif()
find_package(Zephyr REQUIRED HINTS ${ZEPHYR_BASE})
project(nanonng_mqtt_demo)

target_sources(app PRIVATE main.c)

# ═══ Obtain Zephyr compile flags for external build ═══
zephyr_get_include_directories_for_lang_as_string(       C includes)
zephyr_get_system_include_directories_for_lang_as_string(C system_includes)
zephyr_get_compile_definitions_for_lang_as_string(       C definitions)
zephyr_get_compile_options_for_lang_as_string(           C options)

set(build_includes
  -I${PROJECT_BINARY_DIR}/zephyr/include/generated
  -I${PROJECT_BINARY_DIR}/zephyr/include/generated/zephyr
)
string(REPLACE ";" " " build_includes_str "${build_includes}")

set(NNG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(external_cflags
  "${includes} ${system_includes} ${definitions} ${options} ${build_includes_str}")

# ═══ Build NanoNNG as external project ═══
include(ExternalProject)

ExternalProject_Add(nanonng
  SOURCE_DIR     ${NNG_ROOT}
  BINARY_DIR     ${CMAKE_BINARY_DIR}/nanonng_build
  CMAKE_ARGS
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_AR=${CMAKE_AR}
    -DCMAKE_SYSTEM_NAME=Generic
    -DCMAKE_C_COMPILER_WORKS=TRUE
    -DCMAKE_CXX_COMPILER_WORKS=TRUE
    -DCMAKE_C_FLAGS=${external_cflags}
    -DNNG_PLATFORM_POSIX=ON
    -DNNG_PLATFORM_ZEPHYR=ON
    -DNNG_STATIC_LIB=ON
    -DNNG_TESTS=OFF
    -DNNG_TOOLS=OFF
    -DNNG_ENABLE_TLS=OFF
    -DNNG_ENABLE_QUIC=OFF
    -DNNG_TRANSPORT_IPC=OFF
    -DNNG_TRANSPORT_TLS=OFF
    -DNNG_TRANSPORT_WS=OFF
    -DNNG_ENABLE_HTTP=OFF
    -DNNG_ENABLE_SQLITE=OFF
    -DNNG_ENABLE_COMPAT=OFF
    -DNNG_MAX_TASKQ_THREADS=2
    -DNNG_MAX_POLLER_THREADS=1
    -DNNG_MAX_EXPIRE_THREADS=1
    -DNNG_RESOLV_CONCURRENCY=1
  BUILD_COMMAND     ${CMAKE_COMMAND} --build <BINARY_DIR> --target nng
  INSTALL_COMMAND   ""
  BUILD_BYPRODUCTS  <BINARY_DIR>/libnng.a
  EXCLUDE_FROM_ALL
)

# ═══ Import the built library ═══
add_library(nng STATIC IMPORTED GLOBAL)
set_property(TARGET nng PROPERTY IMPORTED_LOCATION
  ${CMAKE_BINARY_DIR}/nanonng_build/libnng.a)
add_dependencies(nng nanonng)

# Ensure Zephyr generated headers exist before NanoNNG cmake configure
if(TARGET zephyr/syscall_list_h_target)
  ExternalProject_Add_StepDependencies(nanonng configure zephyr/syscall_list_h_target)
endif()

# ═══ Link into app ═══
target_include_directories(app PRIVATE ${NNG_ROOT}/include)
target_link_libraries(app PRIVATE nng)
