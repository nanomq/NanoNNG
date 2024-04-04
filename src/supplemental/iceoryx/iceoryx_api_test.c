//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nuts.h>

void
test_iceoryx_start()
{
}

void
test_iceoryx_msg()
{
}

void
test_iceoryx_rw()
{
}

TEST_LIST = {
	{ "iceoryx init and fini", test_iceoryx_start},
	{ "iceoryx alloc a msg", test_iceoryx_msg},
	{ "iceoryx read and write", test_iceoryx_rw},
	{ NULL, NULL },
};
