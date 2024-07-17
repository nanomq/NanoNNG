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

/*
%% client-first-message-bare = [reserved-mext ","] userame "," nonce ["," extensions]
client_first_message_bare(Username) ->
    iolist_to_binary(["n=", Username, ",r=", nonce()]).
*/
uint8_t *
scram_client_first_msg(const char *username)
{
	int sz = strlen(username) + 32; // gs_header + username + nonce
	char *buf = nng_alloc(sizeof(char) * sz);
	sprintf(buf, "%sn=%s,r=%d", gs_header(), username, nonce());
	return (uint8_t *)buf;
}

/*
client_final_message_without_proof(Nonce) ->
    iolist_to_binary(["c=", base64:encode(gs2_header()), ",r=", Nonce]).

client_final_message(Nonce, Proof) ->
    iolist_to_binary([client_final_message_without_proof(Nonce), ",p=", base64:encode(Proof)]).
*/
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

	sprintf(buf, "c=%s,r=%d,p=%s", ghb64, nonce, proofb64);
	return (uint8_t *)buf;
}

/*
server_first_message(Nonce, Salt, IterationCount) ->
    iolist_to_binary(["r=", Nonce, ",s=", base64:encode(Salt), ",i=", integer_to_list(IterationCount)]).
*/
static uint8_t *
scram_server_first_msg(int nonce, const char *salt, int iteration_cnt)
{
	size_t saltb64sz = BASE64_ENCODE_OUT_SIZE(strlen(salt)) + 1;
	char saltb64[saltb64sz];
	if (0 != base64_encode(salt, strlen(salt), saltb64)) {
		return NULL;
	}
	char *buf = nng_alloc(sizeof(char) * (saltb64sz + 64));
	sprintf(buf, "r=%d,s=%d,i=%d", nonce, saltb64, iteration_cnt);
	return (uint8_t *)buf;
}

/*
server_final_message(verifier, ServerSignature) ->
    iolist_to_binary(["v=", base64:encode(ServerSignature)]);
server_final_message(error, Error) ->
    iolist_to_binary(["e=", Error]).
*/
static uint8_t *
scram_server_final_msg(const char * server_sig, int error)
{
	char *buf;
	if (error != 0) {
		buf = nng_alloc(sizeof(char) * 32);
		sprintf(buf, "e=%d", error);
		return buf;
	}
	size_t ssb64sz = BASE64_ENCODE_OUT_SIZE(strlen(ss)) + 1;
	char ssb64[ssb64sz];
	if (0 != base64_encode(server_sig, strlen(server_sig), ssb64)) {
		return NULL;
	}
	buf = nng_alloc(sizeof(char) * (ssb64sz + 32));
	sprintf(buf, "v=%s", ssb64);
	return buf;
}

int
scram_handle_client_first_msg()
{
}

int
scram_handle_server_first_msg()
{
}

int
scram_handle_client_final_msg()
{
}

int
scram_handle_server_final_msg()
{
}

