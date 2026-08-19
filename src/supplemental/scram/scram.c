//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>

#include "scram.h"

#include "nng/supplemental/nanolib/nmq_base64.h"
#include "nng/supplemental/nanolib/log.h"

#define SCRAM_SALT_SZ 64

static int
gen_salt()
{
	return (int) nng_random();
}

static char *
gs_header()
{
	return (char *) "n,,";
}

static int
nonce()
{
	return (int) nng_random();
}

static int
salt_password(char *pwd, int pwdsz, char *salt, int saltsz, int iteration_cnt,
    const EVP_MD *digest, int keysz, char *result)
{
	return PKCS5_PBKDF2_HMAC(pwd, pwdsz, (const unsigned char *) salt,
	    saltsz, iteration_cnt, digest, keysz, (unsigned char *) result);
}

static char *
client_key(const EVP_MD *digest, char *salt_pwd, int sz)
{
	char          *key  = salt_pwd;
	char          *data = "Client Key";
	unsigned char *md = HMAC(digest, key, sz, (const unsigned char *) data,
	    strlen(data), NULL, NULL);
	if (md != NULL) {
		char *result = nng_alloc(sizeof(char) * sz);
		if (result)
			memcpy(result, md, sz);
		return result;
	}
	return NULL;
}

static char *
server_key(const EVP_MD *digest, char *salt_pwd, int sz)
{
	char          *key  = salt_pwd;
	char          *data = "Server Key";
	unsigned char *md = HMAC(digest, key, sz, (const unsigned char *) data,
	    strlen(data), NULL, NULL);
	if (md != NULL) {
		char *result = nng_alloc(sizeof(char) * sz);
		if (result)
			memcpy(result, md, sz);
		return result;
	}
	return NULL;
}

static char *
hash(const EVP_MD *digest, char *data, int sz)
{
	unsigned char *out_hash = nng_alloc(sizeof(char) * EVP_MAX_MD_SIZE);
	if (!out_hash)
		return NULL;

	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
	if (mdctx == NULL) {
		log_error("Failed to create EVP_MD_CTX\n");
		nng_free(out_hash, 0);
		return NULL;
	}

	if (1 != EVP_DigestInit_ex(mdctx, digest, NULL)) {
		log_error("Failed to initialize digest\n");
		EVP_MD_CTX_free(mdctx);
		nng_free(out_hash, 0);
		return NULL;
	}

	if (1 != EVP_DigestUpdate(mdctx, data, sz)) {
		log_error("Failed to update digest\n");
		EVP_MD_CTX_free(mdctx);
		nng_free(out_hash, 0);
		return NULL;
	}

	unsigned int out_len;
	if (1 != EVP_DigestFinal_ex(mdctx, out_hash, &out_len)) {
		EVP_MD_CTX_free(mdctx);
		nng_free(out_hash, 0);
		return NULL;
	}

	EVP_MD_CTX_free(mdctx);
	return (char *) out_hash;
}

static char *
stored_key(const EVP_MD *digest, char *client_key, int sz)
{
	return hash(digest, client_key, sz);
}

static void
xor(char *in1, char *in2, char *out, int len)
{
	for (int i = 0; i < len; ++i) {
		out[i] = in1[i] ^ in2[i];
	}
}

struct scram_ctx {
	char         *pwd;
	int           pwdsz;
	char         *salt;
	char         *salt_pwd;
	const EVP_MD *digest;
	int           digestsz;
	char         *client_key;
	char         *server_key;
	char         *stored_key;
	int           iteration_cnt;

	char *cached_nonce;

	char *client_first_msg;
	char *client_final_msg_without_proof;
	char *client_first_msg_bare;
	char *server_first_msg;
};

static char *
scram_hmac(void *arg, char *key, int keysz, char *data)
{
	struct scram_ctx *ctx = arg;
	unsigned char    *md  = HMAC(ctx->digest, key, keysz,
	    (const unsigned char *) data, strlen(data), NULL, NULL);
	if (md != NULL) {
		char *result = nng_alloc(sizeof(char) * keysz);
		if (result)
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
	if (ctx->pwd)
		nng_free(ctx->pwd, 0);
	if (ctx->salt)
		nng_free(ctx->salt, 0);
	if (ctx->salt_pwd)
		nng_free(ctx->salt_pwd, 0);
	if (ctx->client_key)
		nng_free(ctx->client_key, 0);
	if (ctx->server_key)
		nng_free(ctx->server_key, 0);
	if (ctx->stored_key)
		nng_free(ctx->stored_key, 0);
	if (ctx->cached_nonce)
		nng_free(ctx->cached_nonce, 0);

	if (ctx->client_final_msg_without_proof)
		nng_free(ctx->client_final_msg_without_proof, 0);
	if (ctx->server_first_msg)
		nng_free(ctx->server_first_msg, 0);
	if (ctx->client_first_msg)
		nng_free(ctx->client_first_msg, 0);
	nng_free(ctx, 0);
}

static int
scram_ctx_update(void *arg, char *salt)
{
	struct scram_ctx *ctx   = arg;
	int               keysz = ctx->digestsz;

	ctx->salt = salt;
	if (ctx->salt == NULL) {
		return -1;
	}

	char *salt_pwd = nng_alloc(sizeof(char) * ctx->digestsz);
	if (!salt_pwd)
		return -1;

	int rv =
	    salt_password(ctx->pwd, ctx->pwdsz, ctx->salt, strlen(ctx->salt),
	        ctx->iteration_cnt, ctx->digest, ctx->digestsz, salt_pwd);
	if (rv != 1) {
		log_error("salt password failed %d???\n", rv);
		nng_free(salt_pwd, 0);
		nng_free(ctx->salt, 0);
		ctx->salt = NULL;
		return -2;
	}
	ctx->salt_pwd = salt_pwd;

	ctx->client_key = client_key(ctx->digest, salt_pwd, keysz);
	ctx->server_key = server_key(ctx->digest, salt_pwd, keysz);
	ctx->stored_key = stored_key(ctx->digest, ctx->client_key, keysz);

	return 0;
}

void *
scram_ctx_create(
    char *pwd, int pwdsz, int iteration_cnt, enum SCRAM_digest dig, int salt)
{
	int           rv;
	int           keysz;
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
		log_error("wrong SCRAM_TYPE\n");
		return NULL;
	}
	struct scram_ctx *ctx = nng_alloc(sizeof(struct scram_ctx));
	if (ctx == NULL) {
		log_error("no memory\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(*ctx));

	ctx->pwd = nng_alloc(pwdsz + 1);
	if (ctx->pwd == NULL) {
		nng_free(ctx, 0);
		return NULL;
	}
	memcpy(ctx->pwd, pwd, pwdsz);
	ctx->pwd[pwdsz] = '\0';

	ctx->pwdsz         = pwdsz;
	ctx->digest        = digest;
	ctx->digestsz      = keysz;
	ctx->iteration_cnt = iteration_cnt;

	if (salt == 0)
		return (void *) ctx;

	salt          = gen_salt();
	char *saltstr = nng_alloc(sizeof(char) * SCRAM_SALT_SZ);
	if (saltstr == NULL) {
		nng_free(ctx->pwd, 0);
		nng_free(ctx, 0);
		return NULL;
	}
	snprintf(saltstr, SCRAM_SALT_SZ, "%d", salt);
	if (0 != (rv = scram_ctx_update(ctx, saltstr))) {
		log_error("error in updating ctx %d", rv);
		nng_free(ctx, 0);
		return NULL;
	}

	return (void *) ctx;
}

char *
scram_client_first_msg(void *arg, const char *username)
{
	struct scram_ctx *ctx                   = arg;
	size_t            uname_len             = strlen(username);
	size_t            bare_sz               = uname_len + 32;
	char             *client_first_msg_bare = nng_alloc(bare_sz);
	if (!client_first_msg_bare)
		return NULL;

	snprintf(
	    client_first_msg_bare, bare_sz, "n=%s,r=%d", username, nonce());

	int   sz  = uname_len + SCRAM_SALT_SZ + 64;
	char *buf = nng_alloc(sizeof(char) * sz);
	if (!buf) {
		nng_free(client_first_msg_bare, 0);
		return NULL;
	}

	snprintf(buf, sz, "%s%s", gs_header(), client_first_msg_bare);
	ctx->client_first_msg_bare = buf + strlen(gs_header());
	ctx->client_first_msg      = buf;

	nng_free(client_first_msg_bare, 0);
	return buf;
}

static char *
scram_client_final_msg(char *nonce, const char *proof, int client_proofsz)
{
	char  *gh         = gs_header();
	size_t ghb64sz    = BASE64_ENCODE_OUT_SIZE(strlen(gh)) + 1;
	char  *ghb64      = nng_alloc(ghb64sz);
	size_t proofb64sz = BASE64_ENCODE_OUT_SIZE(client_proofsz) + 1;
	char  *proofb64   = nng_alloc(proofb64sz);

	if (!ghb64 || !proofb64 || ghb64sz == 0 || proofb64sz == 0) {
		if (ghb64)
			nng_free(ghb64, 0);
		if (proofb64)
			nng_free(proofb64, 0);
		return NULL;
	}

	if (0 ==
	        nmq_base64_encode((const unsigned char *) gh, strlen(gh), ghb64, ghb64sz) ||
	    0 ==
	        nmq_base64_encode(
	            (const unsigned char *) proof, client_proofsz, proofb64, proofb64sz)) {
		nng_free(ghb64, 0);
		nng_free(proofb64, 0);
		return NULL;
	}

	size_t bufsz = ghb64sz + proofb64sz + strlen(nonce) + 32;
	char  *buf   = nng_alloc(sizeof(char) * bufsz);
	if (!buf) {
		nng_free(ghb64, 0);
		nng_free(proofb64, 0);
		return NULL;
	}

	snprintf(buf, bufsz, "c=%s,r=%s,p=%s", ghb64, nonce, proofb64);
	nng_free(ghb64, 0);
	nng_free(proofb64, 0);
	return buf;
}

static char *
scram_server_first_msg(char *nonce, const char *salt, int iteration_cnt)
{
	size_t saltb64sz = BASE64_ENCODE_OUT_SIZE(strlen(salt)) + 1;
	char  *saltb64   = nng_alloc(saltb64sz);
	if (saltb64sz == 0 || !saltb64)
		return NULL;

	if (0 ==
	    nmq_base64_encode(
	        (const unsigned char *) salt, strlen(salt), saltb64, saltb64sz)) {
		nng_free(saltb64, 0);
		return NULL;
	}

	size_t bufsz = saltb64sz + strlen(nonce) + 64;
	char  *buf   = nng_alloc(sizeof(char) * bufsz);
	if (buf) {
		snprintf(buf, bufsz, "r=%s,s=%s,i=%d", nonce, saltb64,
		    iteration_cnt);
	}
	nng_free(saltb64, 0);
	return buf;
}

static char *
scram_server_final_msg(const char *server_sig, int sz, int error)
{
	char *buf;
	if (error != 0) {
		buf = nng_alloc(sizeof(char) * 32);
		if (buf)
			snprintf(buf, 32, "e=%d", error);
		return buf;
	}
	size_t ssb64sz = BASE64_ENCODE_OUT_SIZE(sz) + 1;
	char  *ssb64   = nng_alloc(ssb64sz);
	if (ssb64sz == 0 || !ssb64)
		return NULL;

	if (0 ==
	    nmq_base64_encode((const unsigned char *) server_sig, sz, ssb64, ssb64sz)) {
		nng_free(ssb64, 0);
		return NULL;
	}

	size_t bufsz = ssb64sz + 32;
	buf          = nng_alloc(sizeof(char) * bufsz);
	if (buf) {
		snprintf(buf, bufsz, "v=%s", ssb64);
	}
	nng_free(ssb64, 0);
	return buf;
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
	}

	if (len > peekn) {
		int   out_len = len - peekn;
		char *val     = nng_alloc(out_len + 1);
		if (val) {
			memcpy(val, payload + peekn, out_len);
			val[out_len] = '\0';
			return val;
		}
	}
	return NULL;
}

char *
scram_handle_client_first_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx   = arg;
	char             *it    = (char *) msg;
	char             *itend = it + len - 1;
	char             *itnext;
	char *gs2_cbind_flag = get_comma_value(it, itend, &itnext, 0);
	it                   = itnext;
	char *authzid        = get_comma_value(it, itend, &itnext, 0);
	it                   = itnext;

	ctx->client_first_msg = nng_alloc(len + 1);
	if (!ctx->client_first_msg) {
		goto error_out;
	}
	memcpy(ctx->client_first_msg, msg, len);
	ctx->client_first_msg[len] = '\0';
	ctx->client_first_msg_bare = ctx->client_first_msg + (it - msg);

	char *username   = get_comma_value(it, itend, &itnext, 2);
	it               = itnext;
	char *cnonce     = get_comma_value(it, itend, &itnext, 2);
	it               = itnext;
	char *extensions = get_comma_value(it, itend, &itnext, 0);

	int    snonce     = nonce();
	size_t cnonce_len = cnonce ? strlen(cnonce) : 0;
	size_t csnonce_sz = cnonce_len + 32;
	char  *csnonce    = nng_alloc(csnonce_sz);
	if (!csnonce)
		goto error_out_mem;

	snprintf(csnonce, csnonce_sz, "%s%d", cnonce ? cnonce : "", snonce);

	char *salt          = ctx->salt;
	int   iteration_cnt = ctx->iteration_cnt;
	if (salt == NULL) {
		log_error("scram ctx has no salt\n");
		nng_free(csnonce, 0);
		goto error_out_mem;
	}
	char *server_first_msg =
	    scram_server_first_msg(csnonce, salt, iteration_cnt);

	if (server_first_msg) {
		ctx->server_first_msg =
		    nng_alloc(strlen(server_first_msg) + 1);
		if (ctx->server_first_msg)
			strcpy(ctx->server_first_msg, server_first_msg);
	}

	ctx->cached_nonce = nng_alloc(strlen(csnonce) + 1);
	if (ctx->cached_nonce)
		strcpy(ctx->cached_nonce, csnonce);

	nng_free(csnonce, 0);
	if (gs2_cbind_flag)
		nng_free(gs2_cbind_flag, 0);
	if (authzid)
		nng_free(authzid, 0);
	if (username)
		nng_free(username, 0);
	if (cnonce)
		nng_free(cnonce, 0);
	if (extensions)
		nng_free(extensions, 0);
	return server_first_msg;

error_out_mem:
	if (username)
		nng_free(username, 0);
	if (cnonce)
		nng_free(cnonce, 0);
	if (extensions)
		nng_free(extensions, 0);
error_out:
	if (gs2_cbind_flag)
		nng_free(gs2_cbind_flag, 0);
	if (authzid)
		nng_free(authzid, 0);
	return NULL;
}

static char *
peek_client_final_msg_without_proof(const char *msg)
{
	size_t msg_len = strlen(msg);
	char  *m       = nng_alloc(msg_len + 1);
	if (!m)
		return NULL;
	strcpy(m, msg);
	char *end = strstr(m, ",p=");
	if (end)
		*end = '\0';
	return m;
}
char *
scram_handle_client_final_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx    = arg;
	char             *result = NULL;
	char             *it     = (char *) msg;
	char             *itend  = it + len - 1;
	char             *itnext;
	char *gs2_cbind_flag = get_comma_value(it, itend, &itnext, 2);
	it                   = itnext;
	char *csnonce        = get_comma_value(it, itend, &itnext, 2);
	it                   = itnext;
	char *proof          = get_comma_value(it, itend, &itnext, 2);
	it                   = itnext;

	if (!gs2_cbind_flag || !csnonce || !proof) {
		goto cleanup;
	}

	int proofsz = ctx->digestsz;
	if (strlen(proof) * 3 / 4 > proofsz) {
		goto cleanup;
	}

	char *client_final_msg_without_proof =
	    peek_client_final_msg_without_proof(msg);
	if (!client_final_msg_without_proof) {
		goto cleanup;
	}

	if (!ctx->client_first_msg_bare || !ctx->server_first_msg) {
		nng_free(client_final_msg_without_proof, 0);
		goto cleanup;
	}

	size_t authmsg_sz = strlen(ctx->client_first_msg_bare) +
	    strlen(ctx->server_first_msg) +
	    strlen(client_final_msg_without_proof) + 4;
	char *authmsg = nng_alloc(authmsg_sz);
	if (!authmsg) {
		nng_free(client_final_msg_without_proof, 0);
		goto cleanup;
	}
	snprintf(authmsg, authmsg_sz, "%s,%s,%s", ctx->client_first_msg_bare,
	    ctx->server_first_msg, client_final_msg_without_proof);
	log_trace("handle client final authmsg: %s\n", authmsg);

	char *client_sig =
	    scram_hmac(ctx, ctx->stored_key, ctx->digestsz, authmsg);
	char *client_key   = nng_alloc(proofsz);
	char *client_proof = nng_alloc(proofsz + 1);

	if (!client_key || !client_proof ||
	    0 ==
	        nmq_base64_decode(
	            proof, strlen(proof), (unsigned char *) client_proof, proofsz + 1)) {
		if (client_sig)
			nng_free(client_sig, 0);
		if (client_key)
			nng_free(client_key, 0);
		if (client_proof)
			nng_free(client_proof, 0);
		nng_free(client_final_msg_without_proof, 0);
		nng_free(authmsg, authmsg_sz);
		goto cleanup;
	}
	xor(client_proof, client_sig, client_key, proofsz);

	char *hash_client_key = hash(ctx->digest, client_key, ctx->digestsz);
	if (ctx->cached_nonce &&
	    0 == strcmp(csnonce, ctx->cached_nonce) &&
	    hash_client_key && ctx->stored_key &&
	    0 == CRYPTO_memcmp(hash_client_key, ctx->stored_key, ctx->digestsz)) {
		char *server_sig = scram_hmac(ctx, ctx->server_key, ctx->digestsz, authmsg);
		if (server_sig) {
			char *server_final_msg = scram_server_final_msg(server_sig, ctx->digestsz, 0);
			result = server_final_msg;
			nng_free(server_sig, 0);
		}
	}

	if (hash_client_key)
		nng_free(hash_client_key, 0);
	if (client_sig)
		nng_free(client_sig, 0);
	nng_free(client_key, 0);
	nng_free(client_proof, 0);
	nng_free(client_final_msg_without_proof, 0);
	nng_free(authmsg, authmsg_sz);

cleanup:
	if (gs2_cbind_flag)
		nng_free(gs2_cbind_flag, 0);
	if (csnonce)
		nng_free(csnonce, 0);
	if (proof)
		nng_free(proof, 0);
	return result;
}

char *
scram_handle_server_first_msg(void *arg, const char *msg, int len)
{
	struct scram_ctx *ctx   = arg;
	char             *it    = (char *) msg;
	char             *itend = it + len - 1;
	char             *itnext;
	char             *nonce = get_comma_value(it, itend, &itnext, 2);
	it                      = itnext;
	char *saltb64           = get_comma_value(it, itend, &itnext, 2);
	it                      = itnext;
	char *iteration_cnt     = get_comma_value(it, itend, &itnext, 2);

	if (!nonce || !saltb64 || !iteration_cnt) {
		goto cleanup_fields;
	}

	size_t salt_len = strlen(saltb64) * 3 / 4;
	if (salt_len > SCRAM_SALT_SZ || strlen(nonce) > 256) {
		goto cleanup_fields;
	}

	if (ctx->server_first_msg)
		nng_free(ctx->server_first_msg, 0);
	ctx->server_first_msg = nng_alloc(len + 1);
	if (ctx->server_first_msg) {
		memcpy(ctx->server_first_msg, msg, len);
		ctx->server_first_msg[len] = '\0';
	}

	char *salt = nng_alloc(sizeof(char) * SCRAM_SALT_SZ);
	if (!salt)
		goto cleanup_fields;
	memset(salt, 0, SCRAM_SALT_SZ);

	if (0 ==
	    nmq_base64_decode(saltb64, strlen(saltb64), (unsigned char *) salt, SCRAM_SALT_SZ)) {
		nng_free(salt, 0);
		goto cleanup_fields;
	}

	scram_ctx_update(ctx, salt);

	char  *gh      = gs_header();
	size_t ghb64sz = BASE64_ENCODE_OUT_SIZE(strlen(gh)) + 1;
	char  *ghb64   = nng_alloc(ghb64sz);
	if (ghb64sz == 0 || !ghb64 ||
	    0 == nmq_base64_encode((const unsigned char *) gh, strlen(gh), ghb64, ghb64sz)) {
		if (ghb64)
			nng_free(ghb64, 0);
		goto cleanup_fields;
	}

	size_t msg_without_proof_sz          = ghb64sz + strlen(nonce) + 32;
	char *client_final_msg_without_proof = nng_alloc(msg_without_proof_sz);
	if (!client_final_msg_without_proof) {
		nng_free(ghb64, 0);
		goto cleanup_fields;
	}
	snprintf(client_final_msg_without_proof, msg_without_proof_sz,
	    "c=%s,r=%s", ghb64, nonce);
	nng_free(ghb64, 0);

	if (ctx->client_final_msg_without_proof)
		nng_free(ctx->client_final_msg_without_proof, 0);
	ctx->client_final_msg_without_proof =
	    nng_alloc(strlen(client_final_msg_without_proof) + 1);
	if (ctx->client_final_msg_without_proof) {
		strcpy(ctx->client_final_msg_without_proof,
		    client_final_msg_without_proof);
	}

	if (!ctx->client_first_msg_bare || !ctx->server_first_msg) {
		nng_free(client_final_msg_without_proof, 0);
		goto cleanup_fields;
	}

	size_t authmsg_sz = strlen(ctx->client_first_msg_bare) +
	    strlen(ctx->server_first_msg) +
	    strlen(client_final_msg_without_proof) + 4;
	char *authmsg = nng_alloc(authmsg_sz);
	if (!authmsg) {
		nng_free(client_final_msg_without_proof, 0);
		goto cleanup_fields;
	}
	snprintf(authmsg, authmsg_sz, "%s,%s,%s", ctx->client_first_msg_bare,
	    ctx->server_first_msg, client_final_msg_without_proof);

	char *client_sig =
	    scram_hmac(ctx, ctx->stored_key, ctx->digestsz, authmsg);
	int   client_sig_len = ctx->digestsz;
	char *client_proof   = nng_alloc(client_sig_len + 1);

	if (client_sig && client_proof) {
		xor(ctx->client_key, client_sig, client_proof, client_sig_len);
	}

	char *client_final_msg = NULL;
	if (client_proof) {
		client_final_msg = scram_client_final_msg(
		    nonce, client_proof, client_sig_len);
	}

	nng_free(authmsg, authmsg_sz);
	nng_free(client_final_msg_without_proof, 0);
	if (client_proof)
		nng_free(client_proof, 0);
	if (client_sig)
		nng_free(client_sig, 0);

cleanup_fields:
	if (nonce)
		nng_free(nonce, 0);
	if (saltb64)
		nng_free(saltb64, 0);
	if (iteration_cnt)
		nng_free(iteration_cnt, 0);
	return client_final_msg;
}

char *
scram_handle_server_final_msg(void *arg, const char *msg, int len)
{
	char             *result = NULL;
	struct scram_ctx *ctx    = arg;
	char             *it     = (char *) msg;
	char             *itend  = it + len - 1;
	char             *itnext;
	char             *verifier = get_comma_value(it, itend, &itnext, 2);
	it                         = itnext;

	if (!verifier) {
		return NULL;
	}

	if (!ctx->client_first_msg_bare || !ctx->server_first_msg ||
	    !ctx->client_final_msg_without_proof) {
		nng_free(verifier, 0);
		return NULL;
	}

	size_t authmsg_sz = strlen(ctx->client_first_msg_bare) +
	    strlen(ctx->server_first_msg) +
	    strlen(ctx->client_final_msg_without_proof) + 4;
	char *authmsg = nng_alloc(authmsg_sz);
	if (!authmsg) {
		nng_free(verifier, 0);
		return NULL;
	}
	snprintf(authmsg, authmsg_sz, "%s,%s,%s", ctx->client_first_msg_bare,
	    ctx->server_first_msg, ctx->client_final_msg_without_proof);
	log_trace("handle server final authmsg: %s\n", authmsg);

	char *server_sig =
	    scram_hmac(ctx, ctx->server_key, ctx->digestsz, authmsg);
	log_trace("client: server_key %.*s\n", ctx->digestsz, ctx->server_key);
	size_t ssb64sz = BASE64_ENCODE_OUT_SIZE(ctx->digestsz) + 1;
	char  *ssb64   = nng_alloc(ssb64sz);

	if (ssb64sz == 0 || !ssb64 || 0 ==
	        nmq_base64_encode((const unsigned char *) server_sig,
	            ctx->digestsz, ssb64, ssb64sz)) {
		nng_free(authmsg, authmsg_sz);
		if (server_sig)
			nng_free(server_sig, 0);
		if (ssb64)
			nng_free(ssb64, 0);
		nng_free(verifier, 0);
		return NULL;
	}

	if (0 == strcmp(verifier, ssb64)) {
		result = arg;
	}
	nng_free(authmsg, authmsg_sz);
	nng_free(ssb64, 0);
	nng_free(server_sig, 0);
	nng_free(verifier, 0);
	return result;
}