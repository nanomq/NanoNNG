//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng/supplemental/base64/base64.h"

/* TODO Is salt a global static value?
gen_salt() ->
    <<X:128/big-unsigned-integer>> = crypto:strong_rand_bytes(16),
    iolist_to_binary(io_lib:format("~32.16.0b", [X])).
*/
static int
gen_salt()
{
	return (int)nng_random();
}

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

static int
salt_password(char *pwd, int pwdsz, char *salt, int saltsz, int iteration_cnt, const EVP_MD *digest, int keysz, char *result)
{
	return PKCS5_PBKDF2_HMAC(pwd, pwdsz, salt, saltsz, iteration_cnt, digest, keysz, result);
}

/*
client_key(Alg, SaltedPassword) ->
    hmac(Alg, SaltedPassword, <<"Client Key">>).
server_key(Alg, SaltedPassword) ->
    hmac(Alg, SaltedPassword, <<"Server Key">>).
stored_key(Alg, ClientKey) ->
    crypto:hash(Alg, ClientKey).
*/
static char *
client_key(const EVP_MD *digest, char *salt_pwd)
{
	return digest(salt_pwd, "Client Key");
}

static char *
server_key(const EVP_MD *digest, char *salt_pwd)
{
	return digest(salt_pwd, "Server Key");
}

static char *
stored_key(const EVP_MD *digest, char *client_key)
{
	//return digest(client_key); // TODO
	return client_key;
}

struct scram_ctx {
	char *salt;
	char *salt_pwd;
	const EVP_MD *hmac;
	char *client_key;
	char *server_key;
	char *stored_key;
	int   iteration_cnt;

	char *client_first_msg_bare;
	char *server_first_msg;
};

void *scram_ctx_create(char *pwd, int pwdsz, int iteration_cnt, SCRAM_digest dig, int keysz)
{
	int rv;
	const EVP_MD *digest;
	switch (dig) {
		case SCRAM_SHA1:
			digest = EVP_sha1();
			break;
		case SCRAM_SHA256:
			digest = EVP_sha256();
			break;
	}
	struct scram_ctx *ctx = nng_alloc(sizeof(struct scram_ctx));
	if (ctx == NULL)
		return NULL;

	int salt = gen_salt();
	ctx->salt = nng_alloc(sizeof(char) * 32);
	if (ctx->salt == NULL)
		return NULL;
	sprintf(ctx->salt, "%d", salt);

	char *salt_pwd = nng_alloc(sizeof(char) * keysz);
	rv = salt_password(pwd, pwdsz, salt, strlen(salt),
			               iteration_cnt, digest, keysz, salt_pwd);
	if (rv != 0)
		return NULL;
	ctx->salt_pwd = salt_pwd;

	// debug
	for (int i=0; i<keysz; ++i)
		printf("%x", salt_pwd[i]);
	printf(">>> PWD SALT\n");

	ctx->hmac = digest;
	ctx->client_key = client_key(digest, salt_pwd);
	ctx->server_key = server_key(digest, salt_pwd);
	ctx->stored_key = stored_key(digest, ctx->client_key);
	ctx->iteration_cnt = iteration_cnt;

	return (void *)ctx;
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
static char *
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

static int
get_comma_value_len(char *payload)
{
	int len = 0;
	char *it = payload;
	while (it != NULL) {
		if (*it == ',')
			break;
		len ++;
	}
	return len;
}

static const char *
get_next_comma_value(char *payload, const char *payload_end)
{
	char *it = payload;
	while (it != NULL) {
		if (*it == ',')
			break;
		it++;
	}
	if (it == payload_end)
		return NULL;
	return it + 1;
}

// %% = gs2-cbind-flag "," [authzid] "," [reserved-mext ","] userame "," nonce ["," extensions]
char *
scram_handle_client_first_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx = arg;
	char *it = msg;
	char *itend = msg + len;
	char *gs2_cbind_flag   = it;
	int   gs2_cbind_flagsz = get_comma_value_len(it);
	it += gs2_cbind_flagsz;
	char *authzid          = get_next_comma_value(it, itend);
	int   authzidsz        = get_comma_value_len(it);
	it += authzidsz;

	/*
	peek_client_first_message_bare(Bin) ->
    [_, One] = binary:split(Bin, <<",">>),
    [_, Two] = binary:split(One, <<",">>),
    Two.
	*/
	ctx->client_first_msg_bare = it;

	char *reserved_mext    = get_next_comma_value(it, itend);
	int   reserved_mextsz  = get_comma_value_len(it);
	it += reserved_mextsz;
	char *username         = get_next_comma_value(it, itend);
	int   usernamesz       = get_comma_value_len(it);
	it += usernamesz;
	char *cnonce            = get_next_comma_value(it, itend);
	int   cnoncesz          = get_comma_value_len(it);
	it += cnoncesz;
	char *extensions       = get_next_comma_value(it, itend);
	int   extensionssz     = get_comma_value_len(it);
	// parse done
	int snonce = nonce();
	char csnonce[64];
	sprintf(csnonce, "%.*s%d", cnoncesz, cnonce, snonce);
	char *salt = ctx->salt;
	int   iteration_cnt = ctx->iteration_cnt;
	char *server_first_msg = scram_server_first_msg(csnonce, salt, iteration_cnt);
	ctx->server_first_msg = server_first_msg;
	return server_first_msg;
}

/*
peek_client_final_message_without_proof(Bin) ->
    [ClientFinalMessageWithoutProof | _] = binary:split(Bin, <<",p=">>, [trim_all]),
    ClientFinalMessageWithoutProof.
*/
static char *
peek_client_final_msg_without_proof(const char *msg)
{
	return strstr(msg, ",p=");
}

// %% = channel-binding "," nonce ["," extensions] "," proof
char *
scram_handle_client_final_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx = arg;
	char *it = msg;
	char *itend = msg + len;
	char *gs2_cbind_flag   = it;
	int   gs2_cbind_flagsz = get_comma_value_len(it);
	it += gs2_cbind_flagsz;
	char *csnonce          = get_next_comma_value(it, itend);
	int   csnoncesz        = get_comma_value_len(it);
	it += csnoncesz;
	char *proof            = get_next_comma_value(it, itend);
	int   proofsz          = get_comma_value_len(it);
	// parse done
	//AuthMessage = ([ ClientFirstMessageBare,ServerFirstMessage,ClientFinalMessageWithoutProof]),
	char *c_final_msg_without_proof = peek_client_final_msg_without_proof(msg);
	char authmsg[256];
	sprintf(authmsg, "%s,%s,%s",
	    ctx->client_first_msg_bare, ctx->server_first_msg, c_final_msg_without_proof);
	// ClientSignature = hmac(Algorithm, StoredKey, AuthMessage),
	char *client_sig = ctx->hmac(ctx->stored_key, authmsg);
	// ClientKey = crypto:exor(ClientProof, ClientSignature)
	char *client_key = proof ^ client_sig; // TODO xor
	/*
	 case Nonce =:= CachedNonce andalso crypto:hash(Algorithm, ClientKey) =:= StoredKey of
         true ->
             ServerSignature = hmac(Algorithm, ServerKey, AuthMessage),
             ServerFinalMessage = server_final_message(verifier, ServerSignature),
             {ok, ServerFinalMessage};
         false ->
             {error, 'other-error'}
     end;
	*/
	if (0 == strncmp(csnonce, ctx->cached_nonce, csnoncesz) &&
	    0 == strcmp(client_key, ctx->stored_key)) {
		char *server_sig = ctx->hmac(ctx->server_key, authmsg);
		char *server_final_msg = scram_server_final_msg(server_sig, 0);
		return server_final_msg;
	}
	return NULL;
}

int
scram_handle_server_first_msg()
{
}

int
scram_handle_server_final_msg()
{
}

