#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

if (NNG_ENABLE_PARQUET OR ENABLE_LICENSE_STD OR ENABLE_LICENSE_DK)
    nng_sources(aes.c aes.h)
    nng_test(aes_test)
endif ()
