#
# Copyright 2020 Staysail Systems, Inc. <info@staysail.tech>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

nng_sources_if(NNG_SUPP_SQLITE sqlite3.c)
nng_headers_if(NNG_SUPP_SQLITE nng/supplemental/sqlite/sqlite3.h)
nng_headers_if(NNG_SUPP_SQLITE nng/supplemental/sqlite/sqlite3ext.h)
nng_link_libraries_if(NNG_SUPP_SQLITE dl)

nng_test_if(NNG_SUPP_SQLITE sqlite3_test)

