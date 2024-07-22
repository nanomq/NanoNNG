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
	char *pwd      = "public";

	void *ctx = scram_ctx_create(pwd, strlen(pwd), 4096, SCRAM_SHA256);
	NUTS_ASSERT(NULL != ctx);
	uint8_t *first_msg = scram_client_first_msg(ctx, username);
	NUTS_ASSERT(NULL != first_msg);

	// We don't care about the random
	char expect_first_msg[256];
	sprintf(expect_first_msg, "n,,n=%s,r=", username);
	NUTS_ASSERT(0 == strncmp((char *)first_msg, expect_first_msg, strlen(expect_first_msg)));

	printf("first msg:%s\n", first_msg);

	nng_free(first_msg, 0);
	scram_ctx_free(ctx);
}

void
test_handle_client_first_msg(void)
{
	char *username  = "admin";
	char *pwd       = "public";
	char *client_first_msg = "n,,n=admin,r=588996903";

	void *ctx = scram_ctx_create(pwd, strlen(pwd), 4096, SCRAM_SHA256);
	NUTS_ASSERT(NULL != ctx);

	char *server_first_msg =
		scram_handle_client_first_msg(ctx, client_first_msg, strlen(client_first_msg));
	NUTS_ASSERT(NULL != server_first_msg);

	printf("server first msg: %s\n", server_first_msg);

	nng_free(server_first_msg, 0);
	scram_ctx_free(ctx);
	(void)username;
}

void
test_handle_server_first_msg(void)
{
	char *username  = "admin";
	char *pwd       = "public";
	char *server_first_msg = "r=5889969031670468145,s=MTcxMDYxMjE0Mw==,i=4096";

	void *ctx = scram_ctx_create(pwd, strlen(pwd), 4096, SCRAM_SHA256);
	NUTS_ASSERT(NULL != ctx);

	char *client_final_msg =
		scram_handle_server_first_msg(ctx, server_first_msg, strlen(server_first_msg));
	NUTS_ASSERT(NULL != client_final_msg);

	printf("client final msg: %s\n", client_final_msg);

	nng_free(client_final_msg, 0);
	scram_ctx_free(ctx);
	(void)username;
}

void
test_handle_client_final_msg(void)
{
	char *username  = "admin";
	char *pwd       = "public";
	char *client_final_msg = "c=biws,r=5889969031670468145,p=DtmY/yJcVDnfUT4hDRF+pvsG6ec8dctrlNe1XO7er2c=";

	void *ctx = scram_ctx_create(pwd, strlen(pwd), 4096, SCRAM_SHA256);
	NUTS_ASSERT(NULL != ctx);

	char *server_final_msg =
		scram_handle_client_final_msg(ctx, client_final_msg, strlen(client_final_msg));
	NUTS_ASSERT(NULL != server_final_msg);

	printf("server final msg: %s\n", server_final_msg);

	nng_free(server_final_msg, 0);
	scram_ctx_free(ctx);
	(void)username;
}

TEST_LIST = {
	{ "first msg", test_first_msg },
	{ "handle client first msg", test_handle_client_first_msg },
	{ "handle server first msg", test_handle_server_first_msg },
	{ "handle client final msg", test_handle_client_final_msg },
	{ NULL, NULL },
};
