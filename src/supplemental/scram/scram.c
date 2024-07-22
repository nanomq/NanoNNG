//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <string.h>
#include <stdio.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "nng/supplemental/nanolib/base64.h"
#include "nng/supplemental/scram/scram.h"
#include "nng/supplemental/nanolib/log.h"

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
	return (char *)"n,,";
}

static int
nonce()
{
	return (int)nng_random();
}

static int
salt_password(char *pwd, int pwdsz, char *salt, int saltsz, int iteration_cnt, const EVP_MD *digest, int keysz, char *result)
{
	return PKCS5_PBKDF2_HMAC(pwd, pwdsz, (const unsigned char *)salt, saltsz, iteration_cnt, digest, keysz, (unsigned char *)result);
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
client_key(const EVP_MD *digest, char *salt_pwd, int sz)
{
	char *key  = salt_pwd;
	char *data = "Client Key";
	unsigned char *md = HMAC(digest, key, sz, (const unsigned char *)data, strlen(data), NULL, NULL);
	if (md != NULL) {
		char *result = nng_alloc(sizeof(char) * sz);
		memcpy(result, md, sz);
		return result;
	}
	return NULL;
}

static char *
server_key(const EVP_MD *digest, char *salt_pwd, int sz)
{
	char *key  = salt_pwd;
	char *data = "Server Key";
	unsigned char *md = HMAC(digest, key, sz, (const unsigned char *)data, strlen(data), NULL, NULL);
	if (md != NULL) {
		char *result = nng_alloc(sizeof(char) * sz);
		memcpy(result, md, sz);
		return result;
	}
	return NULL;
}

static char *
hash(const EVP_MD *digest, char *data, int sz)
{
	unsigned char *out_hash = nng_alloc(sizeof(char) *EVP_MAX_MD_SIZE);

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        fprintf(stderr, "Failed to create EVP_MD_CTX\n");
        log_error("Failed to create EVP_MD_CTX\n");
        return NULL;
    }

    if (1 != EVP_DigestInit_ex(mdctx, digest, NULL)) {
        fprintf(stderr, "Failed to initialize digest\n");
        log_error("Failed to initialize digest\n");
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    if (1 != EVP_DigestUpdate(mdctx, data, sz)) {
        fprintf(stderr, "Failed to update digest\n");
        log_error("Failed to update digest\n");
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    unsigned int out_len;
    if (1 != EVP_DigestFinal_ex(mdctx, out_hash, &out_len)) {
        fprintf(stderr, "Failed to finalize digest\n");
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    EVP_MD_CTX_free(mdctx);
	return (char *)out_hash;
}

static char *
stored_key(const EVP_MD *digest, char *client_key, int sz)
{
	return hash(digest, client_key, sz);
}

static void
xor(char *in1, char *in2, char *out, int len)
{
	for (int i=0; i<len; ++i) {
		out[i] = in1[i] ^ in2[i];
	}
}

struct scram_ctx {
	char *salt;
	char *salt_pwd;
	const EVP_MD *digest;
	int   digestsz;
	char *client_key;
	char *server_key;
	char *stored_key;
	int   iteration_cnt;

	char *cached_nonce;

	char *client_final_msg_without_proof;
	char *client_first_msg_bare;
	char *server_first_msg;
};

static char *
scram_hmac(void *arg, char *key, int keysz, char *data)
{
	struct scram_ctx *ctx = arg;
	unsigned char *md = HMAC(ctx->digest, key, keysz, (const unsigned char *)data, strlen(data), NULL, NULL);
	if (md != NULL) {
		char *result = nng_alloc(sizeof(char) * keysz);
		memcpy(result, md, keysz);
		return result;
	}
	return NULL;
}

void
scram_ctx_free(void *arg)
{
	struct scram_ctx *ctx = arg;
	if (!ctx)
		return;
	if (ctx->salt)         nng_free(ctx->salt, 0);
	if (ctx->salt_pwd)     nng_free(ctx->salt_pwd, 0);
	if (ctx->client_key)   nng_free(ctx->client_key, 0);
	if (ctx->server_key)   nng_free(ctx->server_key, 0);
	if (ctx->stored_key)   nng_free(ctx->stored_key, 0);
	if (ctx->cached_nonce) nng_free(ctx->cached_nonce, 0);

	if (ctx->client_final_msg_without_proof) nng_free(ctx->client_final_msg_without_proof, 0);
	if (ctx->client_first_msg_bare)          nng_free(ctx->client_first_msg_bare, 0);
	if (ctx->server_first_msg)               nng_free(ctx->server_first_msg, 0);
	nng_free(ctx, 0);
}

void *
scram_ctx_create(char *pwd, int pwdsz, int iteration_cnt, enum SCRAM_digest dig)
{
	int rv;
	int keysz;
	const EVP_MD *digest;
	switch (dig) {
		case SCRAM_SHA1:
			digest = EVP_sha1();
			keysz  = 20; // 160 bits
			break;
		case SCRAM_SHA256:
			digest = EVP_sha256();
			keysz  = 32; // 256 bits
			break;
		default:
			printf("default???\n");
			return NULL;
	}
	struct scram_ctx *ctx = nng_alloc(sizeof(struct scram_ctx));
	if (ctx == NULL) {
		printf("no alloc???\n");
		return NULL;
	}

	int salt = gen_salt();
	ctx->salt = nng_alloc(sizeof(char) * 32);
	if (ctx->salt == NULL) {
		nng_free(ctx, 0);
		return NULL;
	}
	sprintf(ctx->salt, "%d", salt);

	char *salt_pwd = nng_alloc(sizeof(char) * keysz);
	rv = salt_password(pwd, pwdsz, ctx->salt, strlen(ctx->salt),
			               iteration_cnt, digest, keysz, salt_pwd);
	if (rv != 1) {
		printf("salt password failed %d???\n", rv);
		nng_free(salt_pwd, 0);
		nng_free(ctx->salt, 0);
		nng_free(ctx, 0);
		return NULL;
	}
	ctx->salt_pwd = salt_pwd;

	// debug
	for (int i=0; i<keysz; ++i)
		printf("%x", salt_pwd[i] & 0xff);
	printf(">>> PWD SALT\n");

	ctx->digest     = digest;
	ctx->digestsz   = keysz;
	ctx->client_key = client_key(digest, salt_pwd, keysz);
	ctx->server_key = server_key(digest, salt_pwd, keysz);
	ctx->stored_key = stored_key(digest, ctx->client_key, keysz);
	ctx->iteration_cnt = iteration_cnt;
	ctx->cached_nonce  = NULL;

	ctx->client_final_msg_without_proof = NULL;
	ctx->client_first_msg_bare          = NULL;
	ctx->server_first_msg               = NULL;

	return (void *)ctx;
}

/*
%% client-first-message-bare = [reserved-mext ","] userame "," nonce ["," extensions]
client_first_message_bare(Username) ->
    iolist_to_binary(["n=", Username, ",r=", nonce()]).
*/
uint8_t *
scram_client_first_msg(void *arg, const char *username)
{
	struct scram_ctx *ctx = arg;
	char client_first_msg_bare[strlen(username) + 32];
	sprintf(client_first_msg_bare, "n=%s,r=%d", username, nonce());

	int sz = strlen(username) + 32; // gs_header + username + nonce
	char *buf = nng_alloc(sizeof(char) * sz);

	sprintf(buf, "%s%s", gs_header(), client_first_msg_bare);
	ctx->client_first_msg_bare = strdup(client_first_msg_bare);
	return (uint8_t *)buf;
}

/*
client_final_message_without_proof(Nonce) ->
    iolist_to_binary(["c=", base64:encode(gs2_header()), ",r=", Nonce]).

client_final_message(Nonce, Proof) ->
    iolist_to_binary([client_final_message_without_proof(Nonce), ",p=", base64:encode(Proof)]).
*/
static char *
scram_client_final_msg(char *nonce, const char *proof)
{
	char *gh = gs_header();
	size_t ghb64sz = BASE64_ENCODE_OUT_SIZE(strlen(gh)) + 1;
	char ghb64[ghb64sz];
	size_t proofb64sz = BASE64_ENCODE_OUT_SIZE(strlen(proof)) + 1;
	char proofb64[proofb64sz];
	if (0 != base64_encode((const unsigned char *)gh, strlen(gh), ghb64)) {
		return NULL;
	}
	if (0 != base64_encode((const unsigned char *)proof, strlen(proof), proofb64)) {
		return NULL;
	}
	char *buf = malloc(sizeof(char) * (ghb64sz + proofb64sz + 32));

	sprintf(buf, "c=%s,r=%s,p=%s", ghb64, nonce, proofb64);
	return buf;
}

/*
server_first_message(Nonce, Salt, IterationCount) ->
    iolist_to_binary(["r=", Nonce, ",s=", base64:encode(Salt), ",i=", integer_to_list(IterationCount)]).
*/
static char *
scram_server_first_msg(char *nonce, const char *salt, int iteration_cnt)
{
	size_t saltb64sz = BASE64_ENCODE_OUT_SIZE(strlen(salt)) + 1;
	char saltb64[saltb64sz];
	if (0 != base64_encode((const unsigned char *)salt, strlen(salt), saltb64)) {
		return NULL;
	}
	char *buf = nng_alloc(sizeof(char) * (saltb64sz + 64));
	sprintf(buf, "r=%s,s=%s,i=%d", nonce, saltb64, iteration_cnt);
	return buf;
}

/*
server_final_message(verifier, ServerSignature) ->
    iolist_to_binary(["v=", base64:encode(ServerSignature)]);
server_final_message(error, Error) ->
    iolist_to_binary(["e=", Error]).
*/
static char *
scram_server_final_msg(const char * server_sig, int error)
{
	char *buf;
	if (error != 0) {
		buf = nng_alloc(sizeof(char) * 32);
		sprintf(buf, "e=%d", error);
		return buf;
	}
	size_t ssb64sz = BASE64_ENCODE_OUT_SIZE(strlen(server_sig)) + 1;
	char ssb64[ssb64sz];
	if (0 != base64_encode((const unsigned char *)server_sig, strlen(server_sig), ssb64)) {
		return NULL;
	}
	buf = nng_alloc(sizeof(char) * (ssb64sz + 32));
	sprintf(buf, "v=%s", ssb64);
	return buf;
}

static int
get_comma_value_len(char *payload, char *payload_end)
{
	int len = 0;
	char *it = payload;
	while (it != (payload_end + 1)) {
		if (*it == ',')
			break;
		it ++;
		len ++;
	}
	return len;
}

static char *
get_next_comma_value(char *payload, char *payload_end)
{
	char *it = payload;
	while (it != (payload_end + 1)) {
		if (*it == ',')
			break;
		it++;
	}
	if (it == (payload_end + 1))
		return NULL;
	return it + 1;
}

static char *
get_comma_value(char *payload, char *payload_end, char **next_start, int peekn)
{
	int   len = 0;
	char *it  = payload;
	while (it != (payload_end + 1)) {
		if (*it == ',')
			break;
		it++;
		len++;
	}
	*next_start = (it + 1);
	if (it == (payload_end + 1)) {
		*next_start = it;
		if (len > 0)
			return strndup(payload + peekn, len - peekn);
		return NULL;
	}
	if (len > 0)
		return strndup(payload + peekn, len - peekn);
	return NULL;
}

// %% = gs2-cbind-flag "," [authzid] "," [reserved-mext ","] userame "," nonce ["," extensions]
char *
scram_handle_client_first_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx = arg;
	char *it = (char *)msg;
	char *itend = it + len;
	char *itnext;
	char *gs2_cbind_flag   = get_comma_value(it, itend, &itnext, 0);
	it = itnext;
	char *authzid          = get_comma_value(it, itend, &itnext, 0);
	it = itnext;

	/*
	peek_client_first_message_bare(Bin) ->
    [_, One] = binary:split(Bin, <<",">>),
    [_, Two] = binary:split(One, <<",">>),
    Two.
	*/
	ctx->client_first_msg_bare = it;

	//char *reserved_mext    = get_next_comma_value(it, itend);
	//int   reserved_mextsz  = get_comma_value_len(it, itend);
	//it += (reserved_mextsz + 1);
	char *username         = get_comma_value(it, itend, &itnext, 2);
	it = itnext;
	char *cnonce           = get_comma_value(it, itend, &itnext, 2);
	it = itnext;
	char *extensions       = get_comma_value(it, itend, &itnext, 0);
	(void)gs2_cbind_flag;
	(void)authzid;
	//(void)reserved_mext;
	(void)username;
	(void)extensions;
	// parse done
	int snonce = nonce();
	char csnonce[64];
	sprintf(csnonce, "%s%d", cnonce, snonce);
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
	char *it = (char *)msg;
	char *itend = it + len;
	char *gs2_cbind_flag   = it;
	int   gs2_cbind_flagsz = get_comma_value_len(it, itend);
	it += gs2_cbind_flagsz;
	char *csnonce          = get_next_comma_value(it, itend);
	int   csnoncesz        = get_comma_value_len(it, itend);
	it += csnoncesz;
	char *proof            = get_next_comma_value(it, itend);
	int   proofsz          = get_comma_value_len(it, itend);
	(void)gs2_cbind_flag;
	(void)gs2_cbind_flagsz;
	// parse done
	//AuthMessage = ([ ClientFirstMessageBare,ServerFirstMessage,ClientFinalMessageWithoutProof]),
	char *client_final_msg_without_proof = peek_client_final_msg_without_proof(msg);
	char authmsg[256];
	sprintf(authmsg, "%s,%s,%s",
	    ctx->client_first_msg_bare, ctx->server_first_msg, client_final_msg_without_proof);
	// ClientSignature = hmac(Algorithm, StoredKey, AuthMessage),
	char *client_sig = scram_hmac(ctx, ctx->stored_key, ctx->digestsz, authmsg);
	// ClientKey = crypto:exor(ClientProof, ClientSignature)
	char client_key[proofsz];
	xor(proof, client_sig, client_key, proofsz);
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
	char *hash_client_key = hash(ctx->digest, client_key, ctx->digestsz);
	if (0 == strncmp(csnonce, ctx->cached_nonce, csnoncesz) &&
	    0 == strcmp(hash_client_key, ctx->stored_key)) {
		char *server_sig = scram_hmac(ctx, ctx->server_key, ctx->digestsz, authmsg);
		char *server_final_msg = scram_server_final_msg(server_sig, 0);
		return server_final_msg;
	}
	return NULL;
}

/*
client_final_message_without_proof(Nonce) ->
    iolist_to_binary(["c=", base64:encode(gs2_header()), ",r=", Nonce]).
*/
// %% = [reserved-mext ","] nonce "," salt "," iteration-count ["," extensions]
char *
scram_handle_server_first_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx = arg;
	char *it = (char *)msg;
	char *itend = it + len;
	char *nonce            = it;
	int   noncesz          = get_comma_value_len(it, itend);
	it += noncesz;
	char *salt             = get_next_comma_value(it, itend);
	int   saltsz           = get_comma_value_len(it, itend);
	it += saltsz;
	char *iteration_cnt    = get_next_comma_value(it, itend);
	int   iteration_cntsz  = get_comma_value_len(it, itend);
	(void)salt;
	(void)saltsz;
	(void)iteration_cnt;
	(void)iteration_cntsz;
	// parse done
	ctx->server_first_msg = (char *)msg;
	//ClientFinalMessageWithoutProof = client_final_message_without_proof(Nonce),
	char *gh = gs_header();
	size_t ghb64sz = BASE64_ENCODE_OUT_SIZE(strlen(gh)) + 1;
	char ghb64[ghb64sz];
	if (0 != base64_encode((const unsigned char *)gh, strlen(gh), ghb64)) {
		return NULL;
	}
	char client_final_msg_without_proof[32];
	sprintf(client_final_msg_without_proof, "c=%s,r=%s", ghb64, nonce);
	ctx->client_final_msg_without_proof = strdup(client_final_msg_without_proof);
	// authmsg=[ClientFirstMessageBare,ServerFirstMessage,ClientFinalMessageWithoutProof]
	char authmsg[256];
	sprintf(authmsg, "%s,%s,%s",
	    ctx->client_first_msg_bare, msg, client_final_msg_without_proof);
	/*
	SaltedPassword = salted_password(Algorithm, Password, Salt, IterationCount),
    ClientKey = client_key(Algorithm, SaltedPassword),
    StoredKey = stored_key(Algorithm, ClientKey),
    ClientSignature = hmac(Algorithm, StoredKey, AuthMessage),
    ClientProof = crypto:exor(ClientKey, ClientSignature),
	*/
	char *client_sig = scram_hmac(ctx, ctx->stored_key, ctx->digestsz, authmsg);
	int client_sig_len = strlen(client_sig);
	char client_proof[client_sig_len];
	xor(ctx->client_key, client_sig, client_proof, client_sig_len);

	return scram_client_final_msg(nonce, client_proof);
}

// %% = (server-error / verifier) ["," extensions]
char *
scram_handle_server_final_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx = arg;
	char *it = (char *)msg;
	char *itend = it + len;
	char *verifier     = it;
	int   verifiersz   = get_comma_value_len(it, itend);
	it += verifiersz;
	//char *extensions   = get_next_comma_value(it, itend);
	//int   extensionssz = get_comma_value_len(it);
	// parse done
	/*
	ClientFinalMessageWithoutProof = client_final_message_without_proof(Nonce),
	authmsg=[ClientFirstMessageBare,ServerFirstMessage,ClientFinalMessageWithoutProof]
	*/
	char authmsg[256];
	sprintf(authmsg, "%s,%s,%s",
	    ctx->client_first_msg_bare,
	    ctx->server_first_msg,
	    ctx->client_final_msg_without_proof);
	/*
    case Verifier =:= hmac(Algorithm, ServerKey, AuthMessage) of
        true ->
            ok;
        false ->
            {error, 'other-error'}
    end;
	*/
	if (0 == strcmp(verifier, scram_hmac(ctx, ctx->server_key, ctx->digestsz, authmsg))) {
		return NULL; // true
	}
	return arg; // return anything to indicate pass
}

