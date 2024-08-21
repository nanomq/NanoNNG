#
# Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

nng_headers(nng/supplemental/encryption/argon2.h)
if(BUILD_WITH_STATIC_LIBS)
	nng_link_libraries(${CMAKE_CURRENT_SOURCE_DIR}/libargon2.a)
else()
	nng_link_libraries(${CMAKE_CURRENT_SOURCE_DIR}/libargon2.so.1)
endif()
