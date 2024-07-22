//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//


#include <string.h>
#include <stdint.h>

#include "nng/supplemental/scram/scram.h"

#include <nuts.h>

void
test_first_msg(void)
{
	char *username = "admin";
	char *pwd      = "password";

	void *ctx = scram_ctx_create(pwd, strlen(pwd), 4096, SCRAM_SHA1);
	NUTS_ASSERT(NULL != ctx);
	uint8_t *first_msg = scram_client_first_msg(ctx, username);
	NUTS_ASSERT(NULL != first_msg);
	char expect_first_msg[256];
	sprintf(expect_first_msg, "n,,n=%s,r=", username);
	NUTS_ASSERT(0 == strncmp((char *)first_msg, expect_first_msg, strlen(expect_first_msg)));
}

TEST_LIST = {
	{ "first msg", test_first_msg },
	{ NULL, NULL },
};
