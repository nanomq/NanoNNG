//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nuts.h>
#include <string.h>

#include "nng/nng.h"
#include "core/nng_impl.h"

#include "iceoryx_api.h"

void
test_iceoryx_basic()
{
	NUTS_ASSERT(0 == nano_iceoryx_init("test-nanomq-iceoryx-basic"));
	NUTS_ASSERT(0 == nano_iceoryx_fini());
}

void
test_iceoryx_msg()
{
	nng_msg *m;
	char    *icem;

	NUTS_ASSERT(0 == nano_iceoryx_init("test-nanomq-iceoryx-msg"));

	// Create a puber
	nano_iceoryx_puber *puber;
	puber = nano_iceoryx_puber_alloc(
		"test-nanomq-iceoryx-puber",
		"test-iceoryx-service",
		"test-iceoryx-instance",
		"test-iceoryx-topic");
	nng_msg_alloc(&m, 0);

	if (NULL == puber) {
		printf("Failed to alloc puber\n");
	}
	NUTS_ASSERT(puber != NULL);

	uint32_t id = 0x1234;
	char *str = "Hello, It's a test-nanomq-iceoryx-msg.";
	nng_msg_append(m, str, strlen(str));
	nano_iceoryx_msg_alloc((void **)&icem, puber, id, m);

	NUTS_ASSERT(NULL != icem);
	int pos = 0;

	uint32_t sz;
	NNI_GET32(icem + pos, sz);
	pos += NANO_ICEORYX_SZ_BYTES;
	NUTS_ASSERT(sz == (NANO_ICEORYX_SZ_BYTES + NANO_ICEORYX_ID_BYTES) + nng_msg_len(m));

	uint32_t id2;
	NNI_GET32(icem + pos, id2);
	pos += NANO_ICEORYX_ID_BYTES;
	NUTS_ASSERT(id2 == id);

	NUTS_ASSERT(0 == strncmp(icem + pos, str, strlen(str)));

	nano_iceoryx_puber_free(puber);
	nng_msg_free(m);

	nano_iceoryx_fini();
}

void
test_iceoryx_rw()
{
	NUTS_ASSERT(0 == nano_iceoryx_init("test-nanomq-iceoryx-rw"));
	NUTS_ASSERT(0 == nano_iceoryx_fini());
}

TEST_LIST = {
	{ "iceoryx init and fini", test_iceoryx_basic},
	{ "iceoryx alloc a msg", test_iceoryx_msg},
	{ "iceoryx read and write", test_iceoryx_rw},
	{ NULL, NULL },
};
