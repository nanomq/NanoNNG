#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

# iceoryx share memory communication
nng_directory(iceoryx_shm)

nng_sources_if(
	NNG_ENABLE_ICEORYX
    iceoryx_shm.c
)

nng_headers_if(NNG_ENABLE_ICEORYX nng/iceoryx_shm/iceoryx_shm.h)

#nng_test(iceoryx_shm_test)
