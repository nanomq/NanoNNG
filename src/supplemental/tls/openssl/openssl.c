//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/x509v3.h>

// Follow the suggestion from Sliepen. https://stackoverflow.com/questions/69079419/how-i-can-read-more-than-16384-bytes-using-openssl-tls
#define OPEN_BUF_SZ 16000

//#define OPEN_DEBUG 1
//#define OPEN_TRACE 1

#ifdef OPEN_TRACE

#define trace(format, arg...)                                                 \
	do {                                                                  \
		fprintf(stderr, ">>>[%s] " format "\n", __FUNCTION__, ##arg); \
	} while (0)

#else

#define trace(format, arg...)                                                 \
	do {                                                                  \
	} while (0)

#endif

#ifdef OPEN_DEBUG

static void
print_hex(char *str, const uint8_t *data, size_t len)
{
	if (len == 0)
		return;
	fprintf(stderr, " %s (%ld): ", str, len);
	for (size_t i=0; i<len; ++i) fprintf(stderr, "%x ", data[i]);
	fprintf(stderr, "\n");
}

#else

static void
print_hex(char *str, const uint8_t *data, size_t len)
{
	(void) str;
	(void) data;
	(void) len;
}

#endif

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/tls/tls.h"
#include <nng/supplemental/tls/engine.h>

#ifdef TLS_EXTERN_PRIVATE_KEY
#endif

#ifdef TLS_EXTERN_SS_CERTS
#define NNG_OPENSSL_HAVE_PASSWORD 1
#include <nng/supplemental/tls/tee2.h>
#include "openssl/engine.h"
#endif

static bool g_print_handshake = false;

#ifdef TLS_EXTERN_PRIVATE_KEY
#endif // TLS_EXTERN_PRIVATE_KEY

static void
open_log_ssl_error(const char *where, int ssl_error)
{
	unsigned long err;
	char          errbuf[256];
	bool          has_error = false;

	while ((err = ERR_get_error()) != 0) {
		ERR_error_string_n(err, errbuf, sizeof(errbuf));
		log_error("%s ssl_error=%d openssl_error=0x%lx %s",
		    where, ssl_error, err, errbuf);
		has_error = true;
	}

	if (!has_error) {
		log_error("%s ssl_error=%d openssl_error=none", where,
		    ssl_error);
	}
}

static bool
open_ssl_error_is_established_close(unsigned long err)
{
	int reason = ERR_GET_REASON(err);

	return reason == SSL_R_WRONG_VERSION_NUMBER ||
	    reason == SSL_R_SHUTDOWN_WHILE_IN_INIT ||
	    reason == SSL_R_SSLV3_ALERT_BAD_CERTIFICATE;
}

struct nng_tls_engine_conn {
	void    *tls; // parent conn
	SSL     *ssl;
	BIO     *rbio; /* SSL reads from, we write to. */
	BIO     *wbio; /* SSL writes to, we read from. */
	// 2 * OPEN_BUF_SZ should be enough to put encrypted data
	char     rbuf[2 * OPEN_BUF_SZ];
	char     wbuf[2 * OPEN_BUF_SZ];
	char    *wnext;
	int      wnsz;
	int      wntcpsz;
	int      running;
	int      ok;
};

struct nng_tls_engine_config {
	SSL_CTX     *ctx;
	nng_tls_mode mode;
	char        *pass;
	char        *server_name;
	int          auth_mode;
	nni_list     psks;
};

static int open_conn_handshake(nng_tls_engine_conn *ec);

/************************* SSL Connection ***********************/

static void
open_conn_fini(nng_tls_engine_conn *ec)
{
	trace("start");
	SSL_free(ec->ssl);
	trace("end");
}

static int
open_net_read(void *ctx, char *buf, int len) {
	trace("start");
	size_t sz = len;
	int    rv;

	if (g_print_handshake)
		log_info("handshake incomplete: receiving...");
	rv = nng_tls_engine_recv(ctx, (uint8_t *) buf, &sz);
	if (g_print_handshake)
		log_info("handshake incomplete: read rv %d sz %ld/%d", rv, sz, len);
	if (rv == 0)
		log_debug("NNG-TLS-NET-RD" "Read From TCP %ld/%d rv%d", sz, len, rv);
	trace("end");
	switch (rv) {
	case 0:
		return ((int) sz);
	case NNG_EAGAIN:
		return 0 - (SSL_ERROR_WANT_READ);
		// return (WOLFSSL_CBIO_ERR_WANT_READ);
	case NNG_ECLOSED:
		return 0 - (SSL_ERROR_WANT_CONNECT);
		// return (WOLFSSL_CBIO_ERR_CONN_CLOSE);
	case NNG_ECONNSHUT:
		return 0 - (SSL_ERROR_WANT_CONNECT);
		// return (WOLFSSL_CBIO_ERR_CONN_RST);
	default:
		return 0 - (SSL_ERROR_WANT_CONNECT);
		// return (WOLFSSL_CBIO_ERR_GENERAL);
	}
}

static int
open_net_write(void *ctx, const char *buf, int len) {
	trace("start %d", len);
	size_t sz = len;
	int    rv;

	if (g_print_handshake)
		log_info("handshake incomplete: sending...");
	rv = nng_tls_engine_send(ctx, (const uint8_t *) buf, &sz);
	if (g_print_handshake)
		log_info("handshake incomplete: write rv %d sz %ld/%d", rv, sz, len);
	log_debug("NNG-TLS-NET-WR" "Sent To TCP %ld/%d rv%d", sz, len, rv);
	trace("end");
	switch (rv) {
	case 0:
		return ((int) sz);

	case NNG_EAGAIN:
		return 0 - (SSL_ERROR_WANT_WRITE);
		// return (WOLFSSL_CBIO_ERR_WANT_WRITE);
	case NNG_ECLOSED:
		return 0 - (SSL_ERROR_WANT_CONNECT);
		// return (WOLFSSL_CBIO_ERR_CONN_CLOSE);
	case NNG_ECONNSHUT:
		return 0 - (SSL_ERROR_WANT_CONNECT);
		// return (WOLFSSL_CBIO_ERR_CONN_RST);
	default:
		return 0 - (SSL_ERROR_WANT_CONNECT);
		// return (WOLFSSL_CBIO_ERR_GENERAL);
	}
}

static int
open_conn_init(nng_tls_engine_conn *ec, void *tls, nng_tls_engine_config *cfg)
{
	trace("start");
	g_print_handshake = true;
	ec->running = 0;
	ec->ok = 0;
	ec->tls = tls;
	if ((ec->ssl = SSL_new(cfg->ctx)) == NULL) {
		log_error("NNG-TLS-CONN-INIT" "error in new SSL connection");
		return (NNG_ENOMEM); // most likely
	}

	log_info("NNG-TLS-CONN-INIT" "%s",
			cfg->mode == NNG_TLS_MODE_SERVER ? "SSL Server Mode":"SSL Client Mode");

	ec->rbio = BIO_new(BIO_s_mem());
	ec->wbio = BIO_new(BIO_s_mem());
	if (!ec->rbio || !ec->wbio) {
		log_error("NNG-TLS-CONN-INIT" "error in new BIO for connection");
		return (NNG_ENOMEM); // most likely
	}
	SSL_set_bio(ec->ssl, ec->rbio, ec->wbio);

	if (cfg->mode == NNG_TLS_MODE_CLIENT)
		SSL_set_connect_state(ec->ssl);
	else
		SSL_set_accept_state(ec->ssl);

	ec->wnext = NULL;

	if (cfg->server_name != NULL) {
		log_info("set openssl %p SNI to %s", ec->ssl, cfg->server_name);
		SSL_set_tlsext_host_name(ec->ssl, cfg->server_name);
		log_info("set openssl SNI done");
	}
	trace("end");

	return (0);
}

static void
open_conn_close(nng_tls_engine_conn *ec)
{
	trace("start");
	if (ec->wnext)
		nng_free(ec->wnext, 0);
	SSL_shutdown(ec->ssl);
	trace("end");
}

static int
open_conn_handshake(nng_tls_engine_conn *ec)
{
	int rv;
	if (ec->ok == 1) {
		log_warn("handshake is already done");
		return 0;
	}
	log_info("Doing handshake ...");
	rv = SSL_do_handshake(ec->ssl);
	if (rv != 1) {
		rv = SSL_get_error(ec->ssl, rv);
		if (rv == SSL_ERROR_WANT_READ || rv == SSL_ERROR_WANT_WRITE) {
			log_warn("NNG-TLS-CONN-HANDSHAKE"
					"openssl handshake still in process rv%d", rv);
				// continue
		} else if (rv == SSL_ERROR_NONE) {
			log_warn("NNG-TLS-CONN-HANDSHAKE" "should never reach here");
		} else {
			log_warn("NNG-TLS-CONN-HANDSHAKE"
				"openssl handshake still in process rv%d", rv);
			ERR_print_errors_fp(stderr);
			return NNG_ECRYPTO;
		}
	} else {
		goto finished;
	}

	if (rv == SSL_ERROR_WANT_READ || rv == SSL_ERROR_WANT_WRITE) {
		int ensz, sz;
		while ((ensz = open_net_read(ec->tls, ec->wbuf, OPEN_BUF_SZ)) > 0) {
			sz = BIO_write(ec->rbio, ec->wbuf, ensz);
			log_warn("NNG-TLS-CONN-HANDSHAKE" "BIO write sz%d/%d", sz, ensz);
			if (sz < 0) {
				log_debug("NNG-TLS-CONN-HANDSHAKE"
					"bio write failed %d", sz);
				if (!BIO_should_retry(ec->rbio)) {
					log_warn("NNG-TLS-CONN-HANDSHAKE"
						"openssl BIO write failed rv%d", ensz);
					open_log_ssl_error(
							"NNG-TLS-CONN-HANDSHAKE BIO_write", 0);
					return NNG_ECRYPTO;
				}
				continue;
			}
			rv = SSL_do_handshake(ec->ssl);
			if (rv != 1) {
				rv = SSL_get_error(ec->ssl, rv);
				if (rv == SSL_ERROR_WANT_READ || rv == SSL_ERROR_WANT_WRITE) {
					continue;
				} else if (rv == SSL_ERROR_NONE) {
					log_warn("NNG-TLS-CONN-HANDSHAKE" "should never reach here");
				} else {
					log_error("NNG-TLS-CONN-HANDSHAKE"
							"openssl handshake error %d", rv);
					ERR_print_errors_fp(stderr);
					return NNG_ECRYPTO;
				}
			} else {
				goto finished;
			}
		}
		if (ensz < 0) {
			if (ensz != 0 - SSL_ERROR_WANT_READ && ensz != 0 - SSL_ERROR_WANT_WRITE)
				return (NNG_ECLOSED);
		}

		while ((ensz = BIO_read(ec->wbio, ec->rbuf, OPEN_BUF_SZ)) > 0) {
			log_warn("NNG-TLS-CONN-HANDSHAKE" "BIO read rv%d", ensz);
			if (ensz < 0) {
				if (!BIO_should_retry(ec->wbio)) {
					log_warn("NNG-TLS-CONN-HANDSHAKE"
						"openssl BIO read failed rv%d", ensz);
					open_log_ssl_error(
						"NNG-TLS-CONN-HANDSHAKE BIO_read", 0);
					return NNG_ECRYPTO;
				}
				continue;
			}
			sz = open_net_write(ec->tls, ec->rbuf, ensz);
			log_warn("NNG-TLS-CONN-HANDSHAKE" "tcp write want%d real%d", ensz, sz);
			if (sz == 0 - SSL_ERROR_WANT_READ || sz == 0 - SSL_ERROR_WANT_WRITE)
				return (NNG_EAGAIN);
			else if (sz < 0)
				return (NNG_ECLOSED);
			rv = SSL_do_handshake(ec->ssl);
			if (rv != 1) {
				rv = SSL_get_error(ec->ssl, rv);
				if (rv == SSL_ERROR_WANT_READ || rv == SSL_ERROR_WANT_WRITE) {
					continue;
				} else if (rv == SSL_ERROR_NONE) {
					log_warn("NNG-TLS-CONN-HANDSHAKE" "should never reach here");
				} else {
					log_error("NNG-TLS-CONN-HANDSHAKE"
						"openssl handshake error %d", rv);
					ERR_print_errors_fp(stderr);
					return NNG_ECRYPTO;
				}
			} else {
				goto finished;
			}
		}
		log_info("Wait for next handshake ...");
		return NNG_EAGAIN;
	}
	if (rv == SSL_ERROR_NONE) {
finished:
		log_warn("NNG-TLS-CONN-HANDSHAKE"
				"openssl do handshake successfully");
		g_print_handshake = false;
		ec->ok = 1;
		return 0;
	}
	open_log_ssl_error("NNG-TLS-CONN-HANDSHAKE SSL_do_handshake", rv);
	return NNG_ECRYPTO;
}

static int
open_conn_recv(nng_tls_engine_conn *ec, uint8_t *buf, size_t *szp)
{
	trace("start");
	int rv;
	int ensz = OPEN_BUF_SZ;

	rv = open_net_read(ec->tls, ec->wbuf, ensz);
	if (rv == 0 - SSL_ERROR_WANT_READ || rv == 0 - SSL_ERROR_WANT_WRITE) {
		rv = NNG_EAGAIN;
		goto readopenssl;
	}
	else if (rv < 0)
		return (NNG_ECLOSED);

	int written = 0;
	while ((ensz = BIO_write(ec->rbio, ec->wbuf + written, rv - written)) > 0) {
		written += ensz;
		if (written == rv)
			break;
	}
	if (ensz < 0) {
		log_debug("NNG-TLS-CONN-RECV" "bio write result %d", ensz);
		if (!BIO_should_retry(ec->rbio)) {
			log_error("NNG-TLS-CONN-RECV"
				"openssl BIO write failed rv%d", ensz);
			open_log_ssl_error("NNG-TLS-CONN-RECV BIO_write", 0);
			return (NNG_ECRYPTO);
		}
	}
	log_debug("NNG-TLS-CONN-RECV"
			"recv %d from tcp and written %d to BIO", rv, written);

readopenssl:
	ERR_clear_error();
	rv = SSL_read(ec->ssl, buf, (int) *szp);
	if (rv <= 0) {
		int ssl_rv = SSL_get_error(ec->ssl, rv);
		// TODO return codes according openssl documents
		if (ssl_rv == SSL_ERROR_WANT_READ ||
		    ssl_rv == SSL_ERROR_WANT_WRITE) {
			*szp = 0;
		} else if (ssl_rv == SSL_ERROR_ZERO_RETURN) {
			log_debug("NNG-TLS-CONN-RECV"
			    "openssl read closed by TLS close_notify rv%d ssl_rv%d",
			    rv, ssl_rv);
			return (NNG_ECLOSED);
		} else if (ssl_rv == SSL_ERROR_SYSCALL &&
		    ERR_peek_error() == 0) {
			log_debug("NNG-TLS-CONN-RECV"
			    "openssl read closed by peer EOF rv%d ssl_rv%d",
			    rv, ssl_rv);
			return (NNG_ECLOSED);
		} else if (ec->ok && ssl_rv == SSL_ERROR_SSL &&
		    open_ssl_error_is_established_close(ERR_peek_error())) {
			unsigned long err = ERR_peek_error();
			char          errbuf[256];

			ERR_error_string_n(err, errbuf, sizeof(errbuf));
			log_warn("NNG-TLS-CONN-RECV"
			    "openssl read mapped to closed after established TLS rv%d ssl_rv%d openssl_error=0x%lx %s",
			    rv, ssl_rv, err, errbuf);
			ERR_clear_error();
			return (NNG_ECLOSED);
		} else {
			log_error("NNG-TLS-CONN-RECV"
				"openssl read failed rv%d ssl_rv%d", rv,
				ssl_rv);
			open_log_ssl_error("NNG-TLS-CONN-RECV SSL_read", ssl_rv);
			return (NNG_ECRYPTO);
		}
	} else {
		*szp = (size_t) rv;
	}
	print_hex("recv buffer:", (const uint8_t *)buf, *szp);
	if (*szp == 0) {
		trace("end eagain");
		return NNG_EAGAIN;
	}

	trace("end");
	return (0);
}

static int
open_conn_send(nng_tls_engine_conn *ec, const uint8_t *buf, size_t *szp)
{
	int rv;
	int sz = *szp;
	int batchsz = OPEN_BUF_SZ;
	int written2ssl = 0;
	int written2tcp = 0;
	trace("start");

	if (ec->wnext) {
		log_debug("NNG-TLS-CONN-SEND"
			"write last remaining payload first %d", ec->wnsz);
		char *wnext = ec->wnext;
		rv = open_net_write(ec->tls, wnext, ec->wnsz);
		if (rv > 0) {
			ec->wnext = NULL;
			if (rv != ec->wnsz) {
				int dm = ec->wnsz - rv;
				ec->wnext = nng_alloc(sizeof(char) * dm);
				memcpy(ec->wnext, wnext + rv, dm);
				ec->wnsz = dm;
				log_debug("NNG-TLS-CONN-SEND"
					"written%d remain%d bytes to put to kernel", rv, dm);
				if (g_print_handshake)
					log_info("handshake incomplete: written%d remain%d", rv, dm);
				nng_free(wnext, 0);
				return NNG_EAGAIN;
			}
			nng_free(wnext, 0);
			written2tcp = ec->wntcpsz;
			log_debug("writing done%d written2tcp%d", ec->wnsz, written2tcp);
			if (g_print_handshake)
				log_info("handshake incomplete: remain written done %d", rv);
			goto end;
		} else if (rv == 0 - SSL_ERROR_WANT_READ || rv == 0 - SSL_ERROR_WANT_WRITE) {
			if (g_print_handshake)
				log_warn("handshake incomplete: try to write %d, tcp busy", ec->wnsz);
			return (NNG_EAGAIN);
		} else {
			if (g_print_handshake)
				log_error("handshake incomplete: broken tcp connection");
			return (NNG_ECLOSED);
		}
	}

	print_hex("send buffer:", buf, sz);

	while (written2tcp < sz) {
		log_debug("NNG-TLS-CONN-SEND"
			"written2tcp %d sz %d", written2tcp, sz);
		int remain = sz - written2tcp;
		batchsz = OPEN_BUF_SZ > remain ? remain : OPEN_BUF_SZ;

		if ((rv = SSL_write(ec->ssl, buf + written2tcp, batchsz)) <= 0) {
			// TODO return codes according openssl documents
			rv = SSL_get_error(ec->ssl, rv);
			if (rv != SSL_ERROR_WANT_READ && rv != SSL_ERROR_WANT_WRITE) {
				log_error("NNG-TLS-CONN-SEND" "error in ssl write%d", rv);
				return (NNG_ECRYPTO);
			}
			rv = 0;
		}
		// Update the actual length written to ssl
		written2ssl = rv;

		// We would better to read all bufs first then send.
		int ensz;
		int read2buf = 0;
		while ((ensz = BIO_read(ec->wbio, ec->rbuf + read2buf, OPEN_BUF_SZ)) > 0) {
			log_debug("NNG-TLS-CONN-SEND" "BIO read ensz%d", ensz);
			read2buf += ensz;
			if (read2buf > 2 * OPEN_BUF_SZ) {
				log_error("NNG-TLS-CONN-SEND"
					"BIO read buf over that 2*OPEN_BUF_SZ %d", read2buf);
				return NNG_EINTERNAL;
			}
		}
		if (ensz < 0) {
			//trace("ensz%d", ensz);
			if (!BIO_should_retry(ec->wbio)) {
				log_error("NNG-TLS-CONN-SEND"
					"BIO read failed rv%d", ensz);
				return (NNG_ECRYPTO);
			}
		}

		if (g_print_handshake)
			log_info("handshake len: %d", read2buf);

		rv = open_net_write(ec->tls, ec->rbuf, read2buf);
		if (rv > 0) {
			if (rv != read2buf) {
				int dm = read2buf - rv;
				ec->wnext = nng_alloc(sizeof(char) * dm);
				memcpy(ec->wnext, ec->rbuf + rv, dm);
				ec->wnsz = dm;
				log_debug("NNG-TLS-CONN-SEND"
					"tcp%d ssl%d written%d remain%dbytes to put to kernel",
					written2tcp, read2buf, rv, dm);
				if (g_print_handshake)
					log_info("handshake incomplete: tcp%d ssl%d written%d remain%d",
						written2tcp, read2buf, rv, dm);
				// written2tcp += written2ssl; // This may make wnext send after a long time
				// So updated way is as following.
				// Part of block of data sent failed. The return value size will not
				// contains the length of this block. So Upper layer will send again.
				// So the `wnext` will be sent next immediately.
				ec->wntcpsz = written2ssl;
				goto end;
			}
			written2tcp += written2ssl;
			if (g_print_handshake)
				log_info("handshake incomplete: client hello sent%d wrssl%d", rv, written2ssl);
			// A special case before handshake finished
			if (written2tcp == 0) {
				goto end;
			}
		} else if (rv == 0 - SSL_ERROR_WANT_READ || rv == 0 - SSL_ERROR_WANT_WRITE) {
			trace("end2 read2buf%d written2tcp%d", read2buf, written2tcp);
			if (g_print_handshake)
				log_warn("handshake incomplete: tcp is busy, read2buf%d written2tcp%d",
						read2buf, written2tcp);
			if (written2tcp == 0)
				return NNG_EAGAIN;
			*szp = (size_t) written2tcp;
			return 0;
		} else {
			if (g_print_handshake)
				log_error("handshake incomplete: broken tcp connection");
			return (NNG_ECLOSED);
		}
	}
end:
	trace("end written2tcp%d", written2tcp);
	if (written2tcp == 0)
		return NNG_EAGAIN;
	*szp = (size_t) written2tcp;
	return (0);
}

static bool
open_conn_verified(nng_tls_engine_conn *ec)
{
	long rv = SSL_get_verify_result(ec->ssl);
	log_info("NNG-TLS-CONN-VERIFY" "verified result: %ld", rv);
	return (X509_V_OK == rv);
}

/************************* SSL Configuration ***********************/

static void
open_config_fini(nng_tls_engine_config *cfg)
{
	trace("start cfg %p ctx %p", cfg, cfg->ctx);
	SSL_CTX_free(cfg->ctx);
	if (cfg->server_name != NULL) {
		nng_strfree(cfg->server_name);
	}
	if (cfg->pass != NULL) {
		nng_strfree(cfg->pass);
	}
	trace("end");
}

static int
open_config_init(nng_tls_engine_config *cfg, enum nng_tls_mode mode)
{
	int               auth_mode;
	int               nng_auth;
	const SSL_METHOD *method;
	trace("start");

	cfg->mode = mode;
	// TODO NNI_LIST_INIT(&cfg->psks, psk, node);
	if (mode == NNG_TLS_MODE_SERVER) {
		method    = SSLv23_server_method();
		auth_mode = SSL_VERIFY_NONE;
		nng_auth  = NNG_TLS_AUTH_MODE_NONE;
	} else {
		method    = SSLv23_client_method();
		auth_mode = SSL_VERIFY_PEER;
		nng_auth  = NNG_TLS_AUTH_MODE_REQUIRED;
	}

	cfg->ctx = SSL_CTX_new(method);
	if (cfg->ctx == NULL) {
		log_error("NNG-TLS-CFG-INIT" "error in new ctx");
		return (NNG_ENOMEM);
	}
	// Set max/min version TODO

	SSL_CTX_set_verify(cfg->ctx, auth_mode, NULL);
	//SSL_CTX_set_mode(cfg->ctx, SSL_MODE_AUTO_RETRY);
	//SSL_CTX_set_options(cfg->ctx, SSL_OP_ALL|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);

#ifdef TLS_EXTERN_SS_CERTS
	SSL_CTX_set1_sigalgs_list(cfg->ctx, "rsa_pkcs1_sha256");
	SSL_CTX_set_min_proto_version(cfg->ctx, TLS1_2_VERSION); // minimum TLS 1.2
	SSL_CTX_set_max_proto_version(cfg->ctx, TLS1_2_VERSION); // maximum TLS 1.2
#endif

	trace("start end %p ctx %p", cfg, cfg->ctx);
	cfg->auth_mode = nng_auth;
	return (0);
}

static int
open_config_server(nng_tls_engine_config *cfg, const char *name)
{
	char *dup;
	trace("start");
	if ((dup = nng_strdup(name)) == NULL) {
		return (NNG_ENOMEM);
	}
	if (cfg->server_name) {
		nng_strfree(cfg->server_name);
	}
	cfg->server_name = dup;
	log_info("set openssl SNI to %s", name);
	trace("end");
	return (0);
}

static int
open_config_psk(nng_tls_engine_config *cfg, const char *identity,
    const uint8_t *key, size_t key_len)
{
	NNI_ARG_UNUSED(cfg);
	NNI_ARG_UNUSED(identity);
	NNI_ARG_UNUSED(key);
	NNI_ARG_UNUSED(key_len);
	return (0);
}

static int
open_config_auth_mode(nng_tls_engine_config *cfg, nng_tls_auth_mode mode)
{
	cfg->auth_mode = mode;
	// XXX: REMOVE ME
	switch (mode) {
	case NNG_TLS_AUTH_MODE_NONE:
		SSL_CTX_set_verify(cfg->ctx, SSL_VERIFY_NONE, NULL);
		log_info("NNG-TLS-CFG-AUTH" "AUTH MODE: NONE");
		return (0);
	case NNG_TLS_AUTH_MODE_OPTIONAL:
		SSL_CTX_set_verify(cfg->ctx, SSL_VERIFY_PEER, NULL);
		log_info("NNG-TLS-CFG-AUTH" "AUTH MODE: OPTION");
		return (0);
	case NNG_TLS_AUTH_MODE_REQUIRED:
	default:
		SSL_CTX_set_verify(cfg->ctx,
		    SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
		log_info("NNG-TLS-CFG-AUTH" "AUTH MODE: REQUIRE");
		return (0);
	}
	log_error("NNG-TLS-CFG-AUTH" "AUTH MODE: Unknown");
	return (NNG_EINVAL);
}

static int
open_config_ca_chain(
    nng_tls_engine_config *cfg, const char *certs, const char *crl)
{
	size_t len;
	trace("start");

#ifndef NANOMQ_TLS_VENDOR
#define NANOMQ_TLS_VENDOR "VENDOR"
#endif

#ifdef TLS_EXTERN_PRIVATE_KEY
#else
	if (certs == NULL) {
		log_info("open_config_ca_chain" "NULL certs detected!");
	}
	len = strlen(certs);
#endif //TLS_EXTERN_PRIVATE_KEY
	log_warn("cacertlen:%d", len);

	BIO *bio = BIO_new_mem_buf(certs, len);
	if (!bio) {
		log_error("NNG-TLS-CFG-CACHAIN" "Failed to create BIO");
		return (NNG_ENOMEM);
	}

	X509 *cert = NULL;
	X509_STORE *store = X509_STORE_new();

	while ((cert = PEM_read_bio_X509(bio, NULL, 0, NULL)) != NULL) {
		if (X509_STORE_add_cert(store, cert) == 0) {
			log_error("NNG-TLS-CFG-CACHAIN" "Failed to add certificate to store");
			X509_free(cert);
			BIO_free(bio);
			return (NNG_ECRYPTO);
		}
		X509_free(cert);
	}
	if (cert == NULL) {
		unsigned long err = ERR_peek_last_error();
		if (ERR_GET_LIB(err) == ERR_LIB_PEM &&
			ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
			ERR_clear_error(); /* normal EOF */
		}
	}
	SSL_CTX_set_cert_store(cfg->ctx, store);

	BIO_free(bio);

#ifdef TLS_EXTERN_PRIVATE_KEY
#endif //TLS_EXTERN_PRIVATE_KEY

	if (crl == NULL) {
		trace("end without crl");
		return (0);
	}

#ifdef NNG_OPENSSL_HAVE_CRL
	log_warn("CRL is NOT supported yet");
	/* TODO
	len = strlen(crl);
	rv  = wolfSSL_CTX_LoadCRLBuffer(
	    cfg->ctx, (void *) crl, len, SSL_FILETYPE_PEM);
	if (rv != SSL_SUCCESS) {
	        return (NNG_ECRYPTO);
	}
	*/
#else
#endif
	trace("end");

	return (0);
}

#if NNG_OPENSSL_HAVE_PASSWORD
static int
open_get_password(char *passwd, int size, int rw, void *ctx)
{
	// password is *not* NUL terminated in wolf
	trace("start");
	nng_tls_engine_config *cfg = ctx;
	size_t                 len;

	(void) rw;

	if (cfg->pass == NULL) {
		return (0);
	}
	len = strlen(cfg->pass); // Our "ctx" is really the password.
	if (len > (size_t) size) {
		len = size;
	}
	memcpy(passwd, cfg->pass, len);
	trace("end");
	return (len);
}
#endif

static int
open_config_own_cert(nng_tls_engine_config *cfg, const char *cert,
    const char *key, const char *pass)
{
	int len;
	int rv = 0;
	BIO *biokey = NULL;
	BIO *biocert = NULL;
	X509 *xcert = NULL;
	EVP_PKEY *pkey = NULL;

#if NNG_OPENSSL_HAVE_PASSWORD
	char *dup = NULL;

#ifdef TLS_EXTERN_SS_CERTS
	get_engin_info_in_passwd(pass);
	static ENGINE * engineptr = NULL;
	char *engine_name = tee_get_engine_name();
	char *engine_path = tee_get_engine_path();
	if ((NULL == engine_name) && (NULL == engine_path)) {
		if (pass != NULL) {
			if ((dup = nng_strdup(pass)) == NULL) {
				return (NNG_ENOMEM);
			}
		}
	}
#else
	if (pass != NULL) {
		if ((dup = nng_strdup(pass)) == NULL) {
			return (NNG_ENOMEM);
		}
	}
#endif /* TLS_EXTERN_SS_CERTS */

	if (cfg->pass != NULL) {
		nng_strfree(cfg->pass);
	}
	cfg->pass = dup;
	SSL_CTX_set_default_passwd_cb_userdata(cfg->ctx, cfg);
	SSL_CTX_set_default_passwd_cb(cfg->ctx, open_get_password);
#else
	(void) pass;
#endif

#ifdef TLS_EXTERN_SS_CERTS
	if ((NULL != engine_name) || (NULL != engine_path)) {
		// loading engine
		if ((NULL == engineptr) &&
		    (NULL == (engineptr = engine_init(engine_name, engine_path)))) {
			log_error("engine_init is failed");
			rv = NNG_ENOMEM;

			goto ENGINE_OVER;
		}

		if (!loading_owner_cert_from_engine(engineptr, cfg->ctx)) {
			log_error("Failed to loading_owner_cert_from_engine");
			rv = NNG_ENOMEM;

			goto ENGINE_OVER;
		}
		if (!loading_owner_key_from_engine(engineptr, cfg->ctx)) {
			log_error("Failed to loading_owner_key_from_engine");
			rv = NNG_ENOMEM;

			goto ENGINE_OVER;
		}

		if (SSL_CTX_check_private_key(cfg->ctx) != 1) {
			log_error("Failed to check key in SSL_CTX");
			rv = NNG_ECRYPTO;
		}

	ENGINE_OVER:
		if (NULL != engineptr) {
			log_info("free engineptr");
			ENGINE_finish(engineptr);
			ENGINE_free(engineptr);
			engineptr = NULL;
		}

		if (NULL != engine_name) {
			nng_free(engine_name, 0);
			engine_name = NULL;
		}

		if (NULL != engine_path) {
			nng_free(engine_path, 0);
			engine_path = NULL;
		}

		return rv;
	}
#endif /* TLS_EXTERN_SS_CERTS */

#ifdef TLS_EXTERN_PRIVATE_KEY
#else
	char *cert1 = cert;
	len = strlen(cert1);
#endif // TLS_EXTERN_PRIVATE_KEY
	log_warn("certlen:%d", len);
	biocert = BIO_new_mem_buf(cert1, len);
	if (!biocert) {
		log_error("NNG-TLS-CFG-OWNCHAIN" "Failed to create BIO");
		rv = NNG_ENOMEM;
		goto error;
	}

#ifdef TLS_EXTERN_PRIVATE_KEY
#else
	xcert = PEM_read_bio_X509(biocert, NULL, 0, NULL);
#endif // TLS_EXTERN_PRIVATE_KEY

	log_info("ctx %p cert %p rv%d", cfg->ctx, xcert, rv);
	if ((rv = SSL_CTX_use_certificate(cfg->ctx, xcert)) <= 0) {
		log_error("NNG-TLS-CFG-OWNCHAIN" "Failed to set certificate to SSL_CTX %d", rv);
		ERR_print_errors_fp(stderr);
		rv = NNG_EINVAL;
		goto error;
	}
	rv = 0;

#ifdef TLS_EXTERN_PRIVATE_KEY
#endif

#ifdef TLS_EXTERN_PRIVATE_KEY

#else
	len = strlen(key);
	log_warn("keylen:%d", len);
	biokey = BIO_new_mem_buf(key, len);
	if (!biokey) {
		log_error("NNG-TLS-CFG-OWNCHAIN" "Failed to create key BIO");
		rv = NNG_ENOMEM;
		goto error;
	}
	pkey = PEM_read_bio_PrivateKey(biokey, NULL, NULL, NULL);
	if (!pkey) {
		log_error("NNG-TLS-CFG-OWNCHAIN" "Failed to load key from buffer");
		rv = NNG_EINVAL;
		goto error;
	}
	if (SSL_CTX_use_PrivateKey(cfg->ctx, pkey) <= 0) {
		log_error("NNG-TLS-CFG-OWNCHAIN" "Failed to set key to SSL_CTX");
		rv = NNG_EINVAL;
		goto error;
	}

	if (SSL_CTX_check_private_key(cfg->ctx) != 1) {
		log_error("NNG-TLS-CFG-OWNCHAIN" "Failed to check key in SSL_CTX");
		ERR_print_errors_fp(stderr);
		rv = NNG_ECRYPTO;
		goto error;
	}
#endif // TLS_EXTERN_PRIVATE_KEY

error:
#ifdef TLS_EXTERN_PRIVATE_KEY
#endif // TLS_EXTERN_PRIVATE_KEY
	if (xcert)
		X509_free(xcert);
	if (biocert)
		BIO_free(biocert);
	if (pkey)
		EVP_PKEY_free(pkey);
	if (biokey)
		BIO_free(biokey);

	log_debug("NNG-TLS-CFG-CACHAIN" "done setting");
	trace("end");
	return rv;
}

static int
open_config_version(nng_tls_engine_config *cfg, nng_tls_version min_ver,
    nng_tls_version max_ver)
{
	if ((min_ver > max_ver) || (max_ver > NNG_TLS_1_3)) {
		return (NNG_ENOTSUP);
	}
	// TODO
	(void) cfg;

	return (0);
}

static nng_tls_engine_config_ops open_config_ops = {
	.init     = open_config_init,
	.fini     = open_config_fini,
	.size     = sizeof(nng_tls_engine_config),
	.auth     = open_config_auth_mode,
	.ca_chain = open_config_ca_chain,
	.own_cert = open_config_own_cert,
	.server   = open_config_server,
	.psk      = open_config_psk,
	.version  = open_config_version,
};

static nng_tls_engine_conn_ops open_conn_ops = {
	.size      = sizeof(nng_tls_engine_conn),
	.init      = open_conn_init,
	.fini      = open_conn_fini,
	.close     = open_conn_close,
	.recv      = open_conn_recv,
	.send      = open_conn_send,
	.handshake = open_conn_handshake,
	.verified  = open_conn_verified,
};

static nng_tls_engine open_engine = {
	.version     = NNG_TLS_ENGINE_VERSION,
	.config_ops  = &open_config_ops,
	.conn_ops    = &open_conn_ops,
	.name        = "open",
	.description = "OpenSSL 1.1.1",
	.fips_mode   = false, // commercial users only
};

int
nng_tls_engine_init_open(void)
{
	int rv;
	SSL_library_init();
	SSL_load_error_strings();
	rv = OpenSSL_add_ssl_algorithms();

#if OPENSSL_VERSION_MAJOR < 3
	ERR_load_BIO_strings(); // deprecated since OpenSSL 3.0
#endif
	ERR_load_crypto_strings();

	switch (rv) {
	case 1:
		break;
	default:
		// Best guess...
		EVP_cleanup();
		return (NNG_EINTERNAL);
	}
	return (nng_tls_engine_register(&open_engine));
}

void
nng_tls_engine_fini_open(void)
{
	trace("start");
	EVP_cleanup();
	trace("end");
}
