//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng/supplemental/base64/base64.h"

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
	return (uint8_t *)buf;
}

static uint8_t *
scram_client_final_msg(int nonce, const char *proof)
{
	char *gh = gs_header();
	size_t ghb64sz = BASE64_ENCODE_OUT_SIZE(strlen(gh)) + 1;
	char ghb64[ghb64sz];
	size_t proofb64sz = BASE64_ENCODE_OUT_SIZE(strlen(proof)) + 1;
	char proofb64[proofb64sz];
	if (0 != base64_encode(gh, strlen(gh), ghb64)) {
		return NULL;
	}
	if (0 != base64_encode(proof, strlen(proof), proofb64)) {
		return NULL;
	}
	char *buf = malloc(size(char) * (ghb64sz + proofb64sz + 32));

	sprintf(buf, "c=%s,r=%d,p=%s", ghb64, nonce(), proofb64);
	return (uint8_t *)buf;
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

