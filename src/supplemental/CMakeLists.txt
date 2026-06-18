#
# Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

nng_directory(supplemental)

add_subdirectory(base64)
if (NNG_SUPP_HTTP)
    add_subdirectory(http)
endif()
add_subdirectory(sha1)
add_subdirectory(tls)
add_subdirectory(util)
# Always add websocket so that stub functions (stub.c) are available
# even when the full transport is disabled (stream.c references its symbols).
add_subdirectory(websocket)
add_subdirectory(mqtt)
if (NNG_ENABLE_SQLITE)
    add_subdirectory(sqlite)
endif()
add_subdirectory(nanolib)
if (NNG_ENABLE_QUIC)
    add_subdirectory(quic)
endif()
# iceoryx and scram are not needed on Zephyr
if (NOT NNG_PLATFORM_ZEPHYR)
    add_subdirectory(iceoryx)
    if (NNG_ENABLE_SCRAM)
        add_subdirectory(scram)
    endif()
endif()
