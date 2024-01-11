#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.

cmake_minimum_required (VERSION 2.8.12)

project(exchange_consumer)

find_package(nng CONFIG REQUIRED)

add_executable(exchange_consumer exchange_consumer.c)
target_link_libraries(exchange_consumer nng)

target_compile_definitions(exchange_consumer PRIVATE NNG_ELIDE_DEPRECATED)
