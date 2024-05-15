#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

# iceoryx share memory communication
#nng_directory(iceoryx_shm)


if (NNG_ENABLE_ICEORYX)
    nng_headers(nng/iceoryx_shm/iceoryx_shm.h)
    nng_sources(iceoryx_shm.c)

    #nng_test(iceoryx_shm_test)
endif ()

