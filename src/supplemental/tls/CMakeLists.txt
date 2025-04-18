#
# Copyright 2020 Staysail Systems, Inc. <info@staysail.tech>
# Copyright 2018 Capitar IT Group BV <info@capitar.com>
# Copyright 2019 Devolutions <info@devolutions.net>
#

#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

if (NNG_ENABLE_TLS)
    set(NNG_TLS_ENGINES mbed none)
    # We assume Mbed for now.  (Someday replaced perhaps with Bear.)
    set(NNG_TLS_ENGINE mbed CACHE STRING "TLS engine to use.")
    set_property(CACHE NNG_TLS_ENGINE PROPERTY STRINGS ${NNG_TLS_ENGINES})
else ()
    set(NNG_TLS_ENGINE none)
endif ()

# default TLS implementation for now is Mbed.
nng_headers(nng/supplemental/tls/tls.h)
nng_headers(nng/supplemental/tls/engine.h)

if (NOT NNG_TLS_ENGINE STREQUAL "none")
    nng_test(tls_test)
endif ()

add_subdirectory(mbedtls)
add_subdirectory(openssl)

nng_sources(tls_common.c)
nng_sources(tls_api.h)
