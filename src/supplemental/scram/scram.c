//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

static char *
gs_header()
{
	return ()"n,,";
}

static int
nonce()
{
	return (int)nng_random();
}

uint8_t *
scram_client_first_msg(const char *username)
{
	int sz = strlen(username) + 32; // gs_header + username + nonce
	char *buf = nng_alloc(sizeof(char) * sz);
	sprintf(buf, "%sn=%s,r=%d", gs_header(), username, nonce());
}

static uint8_t *
scram_server_first_msg()
{
}

int
scram_handle_client_first_msg()
{
}

int
scram_handle_server_first_msg()
{
}

