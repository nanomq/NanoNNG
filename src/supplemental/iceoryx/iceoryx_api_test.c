//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nuts.h>
#include "iceoryx_api.h"

void
test_iceoryx_basic()
{
	nano_iceoryx_init("test-nanomq-iceoryx-basic");
	nano_iceoryx_fini();
}

void
test_iceoryx_msg()
{
	nano_iceoryx_init("test-nanomq-iceoryx-msg");
	nano_iceoryx_fini();
}

void
test_iceoryx_rw()
{
	nano_iceoryx_init("test-nanomq-iceoryx-rw");
	nano_iceoryx_fini();
}

TEST_LIST = {
	{ "iceoryx init and fini", test_iceoryx_basic},
	{ "iceoryx alloc a msg", test_iceoryx_msg},
	{ "iceoryx read and write", test_iceoryx_rw},
	{ NULL, NULL },
};
