//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// #if defined(NNG_ENABLE_QUIC) // && defined(NNG_QUIC_MSQUIC)
//
// Note.
// Quic connection is only visible in nng stream.
// Each nng stream is linked to a quic stream.
// nng dialer is linked to quic connection.
// The quic connection would be established when the first
// nng stream with same URL is created.
// The quic connection would be free if all nng streams
// closed.

#include "quic_api.h"
#include "core/nng_impl.h"
#include "msquic.h"

#include "nng/mqtt/mqtt_client.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "supplemental/mqtt/mqtt_msg.h"

#include "openssl/pem.h"
#include "openssl/x509.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "nng/mqtt/mqtt_client.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "supplemental/mqtt/mqtt_msg.h"

#include "openssl/pem.h"
#include "openssl/x509.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct nni_quic_conn {
	nng_stream      stream;
	nni_list        readq;
	nni_list        writeq;
	bool            closed;
	nni_mtx         mtx;
	nni_aio *       dial_aio;
	// nni_aio *       qstrmaio; // Link to msquic_strm_cb
	nni_quic_dialer *dialer;

	// MsQuic
	HQUIC           qstrm; // quic stream
	uint8_t         reason_code;

	nni_reap_node   reap;
};

static const QUIC_API_TABLE *MsQuic = NULL;

// Config for msquic
static const QUIC_REGISTRATION_CONFIG quic_reg_config = {
	"mqtt",
	QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static const QUIC_BUFFER quic_alpn = {
	sizeof("mqtt") - 1,
	(uint8_t *) "mqtt"
};

HQUIC registration;
HQUIC configuration;

static int  msquic_open();
static void msquic_close();
static int  msquic_conn_open(const char *host, const char *port, nni_quic_dialer *d);
static void msquic_conn_close(HQUIC qconn, int rv);
static void msquic_conn_fini(HQUIC qconn);
static int  msquic_strm_open(HQUIC qconn, nni_quic_dialer *d);
static void msquic_strm_close(HQUIC qstrm);
static void msquic_strm_fini(HQUIC qstrm);
static void msquic_strm_recv_start(HQUIC qstrm);

static void quic_dialer_cb(void *arg);
static void quic_stream_error(void *arg, int err);
static void quic_stream_close(void *arg);
static void quic_stream_dowrite(nni_quic_conn *c);

static QUIC_STATUS verify_peer_cert_tls(QUIC_CERTIFICATE* cert, QUIC_CERTIFICATE* chain, char *ca);

static QUIC_STATUS
verify_peer_cert_tls(QUIC_CERTIFICATE* cert, QUIC_CERTIFICATE* chain, char *ca)
{
	// local ca
	X509_LOOKUP *lookup = NULL;
	X509_STORE *trusted = NULL;
	trusted = X509_STORE_new();
	if (trusted == NULL) {
		return QUIC_STATUS_ABORTED;
	}
	lookup = X509_STORE_add_lookup(trusted, X509_LOOKUP_file());
	if (lookup == NULL) {
		X509_STORE_free(trusted);
		trusted = NULL;
		return QUIC_STATUS_ABORTED;
	}

	// if (!X509_LOOKUP_load_file(lookup, cacertfile, X509_FILETYPE_PEM)) {
	if (!X509_LOOKUP_load_file(lookup, ca, X509_FILETYPE_PEM)) {
		log_warn("No load cacertfile be found");
		X509_STORE_free(trusted);
		trusted = NULL;
	}
	if (trusted == NULL) {
		return QUIC_STATUS_ABORTED;
	}

	// @TODO peer_certificate_received
	// Only with QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED
	// set
	// assert(QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED == Event->Type);

	// Validate against CA certificates using OpenSSL API:s
	X509 *crt = (X509 *) cert;
	X509_STORE_CTX *x509_ctx = (X509_STORE_CTX *) chain;
	STACK_OF(X509) *untrusted = X509_STORE_CTX_get0_untrusted(x509_ctx);

	if (crt == NULL)
		return QUIC_STATUS_BAD_CERTIFICATE;

	X509_STORE_CTX *ctx = X509_STORE_CTX_new();
	// X509_STORE_CTX_init(ctx, c_ctx->trusted, crt, untrusted);
	X509_STORE_CTX_init(ctx, trusted, crt, untrusted);
	int res = X509_verify_cert(ctx);
	X509_STORE_CTX_free(ctx);

	X509_STORE_free(trusted);
	trusted = NULL;

	if (res <= 0) {
		log_error("rv %d: %s", res, X509_verify_cert_error_string(ctx));
		return QUIC_STATUS_BAD_CERTIFICATE;
	} else
		return QUIC_STATUS_SUCCESS;

	/* @TODO validate SNI */

/*
	int rv;
	uint32_t flags = 0;
	// cert and chain are as quic_buffer when QUIC_CREDENTIAL_FLAG_USE_PORTABLE_CERTIFICATES is set
	// refer. https://github.com/microsoft/msquic/blob/main/docs/api/QUIC_CONNECTION_EVENT.md#quic_connection_event_peer_certificate_received
	QUIC_BUFFER *ce = (QUIC_BUFFER *)cert;
	QUIC_BUFFER *ch = (QUIC_BUFFER *)chain;
	mbedtls_x509_crt crt;
	mbedtls_x509_crt chn;
	mbedtls_x509_crt_init(&crt);
	mbedtls_x509_crt_init(&chn);
	log_info("chain %p %d cert %p %d\n", ch->Buffer, ch->Length, ce->Buffer, ce->Length);
	mbedtls_x509_crt_parse_der(&crt, ce->Buffer, ce->Length);
	mbedtls_x509_crt_parse(&chn, ch->Buffer, ch->Length);
	rv = mbedtls_x509_crt_verify(&chn, &crt, NULL, NULL, &flags, my_verify, NULL);
	if (rv != 0) {
		char vrfy_buf[512];
		log_warn(" failed\n");
		mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  ! ", flags);
		log_warn("%s\n", vrfy_buf);
	} else {
		log_warn(" Verify OK\n");
	}
	if (rv != 0)
		log_warn("Error: 0x%04x; flag: %u\n", rv, flags);
	return QUIC_STATUS_SUCCESS;
*/

}

/***************************** MsQuic Dialer ******************************/

int
nni_quic_dialer_init(void **argp)
{
	nni_quic_dialer *d;

	if ((d = NNI_ALLOC_STRUCT(d)) == NULL) {
		return (NNG_ENOMEM);
	}

	nni_mtx_init(&d->mtx);
	d->closed  = false;
	d->currcon = NULL;
	d->ca      = NULL;
	d->cacert  = NULL;
	nni_aio_alloc(&d->qconaio, quic_dialer_cb, (void *)d);
	nni_aio_list_init(&d->connq);
	nni_atomic_init_bool(&d->fini);
	nni_atomic_init64(&d->ref);
	nni_atomic_inc64(&d->ref);

	// 0RTT is disabled by default
	d->enable_0rtt = false;
	// multi_stream is disabled by default
	d->enable_mltstrm = false;

	memset(&d->settings, 0, sizeof(QUIC_SETTINGS));

	d->priority = -1;
	d->qidle_timeout = QUIC_IDLE_TIMEOUT_DEFAULT;
	d->settings.IsSet.IdleTimeoutMs = TRUE;
	d->settings.IdleTimeoutMs = d->qidle_timeout * 1000;

	d->qkeepalive = QUIC_KEEPALIVE_DEFAULT;
	d->settings.IsSet.KeepAliveIntervalMs = TRUE;
	d->settings.KeepAliveIntervalMs = d->qkeepalive * 1000;

	*argp = d;
	return 0;
}

static void
quic_dialer_strm_cancel(nni_aio *aio, void *arg, int rv)
{
	nni_quic_dialer *d = arg;
	nni_quic_conn *  c;

	nni_mtx_lock(&d->mtx);
	if ((!nni_aio_list_active(aio)) ||
	    ((c = nni_aio_get_prov_data(aio)) == NULL)) {
		nni_mtx_unlock(&d->mtx);
		return;
	}
	nni_aio_list_remove(aio);
	c->dial_aio = NULL;
	nni_aio_set_prov_data(aio, NULL);
	nni_mtx_unlock(&d->mtx);

	nni_aio_finish_error(aio, rv);
	nng_stream_free(&c->stream);
}

static void
quic_dialer_cancel(nni_aio *aio, void *arg, int rv)
{
	nni_quic_dialer *d = arg;
	nni_quic_conn *  c;
	nni_aio *        cdaio = NULL;

	nni_mtx_lock(&d->mtx);
	if ((c = d->currcon) != NULL) {
		d->currcon = NULL;
		cdaio = c->dial_aio;
		c->dial_aio = NULL;
		nni_aio_set_prov_data(cdaio, NULL);
	}
	nni_mtx_unlock(&d->mtx);

	if (cdaio) {
		nni_aio_finish_error(cdaio, rv);
	}
	nni_aio_finish_error(aio, rv);
}

static void
quic_dialer_cb(void *arg)
{
	nni_quic_dialer *d = arg;
	int              rv;
	nni_aio *        aio;
	nni_quic_conn *  c;

	// pass rv to upper aio if error happened in connecting
	rv = nni_aio_result(d->qconaio);

	nni_mtx_lock(&d->mtx);

	if (d->closed) {
		nni_mtx_unlock(&d->mtx);
		return;
	}

	c = d->currcon;
	if (c == NULL) {
		// abort this cb
		nni_mtx_unlock(&d->mtx);
		return;
	}
	aio = c->dial_aio;
	if ((aio == NULL) || (!nni_aio_list_active(aio))) {
		// This should never happened
		log_error("Found aio reuse error");
		nni_mtx_unlock(&d->mtx);
		return;
	}

	if (rv != 0) {
		// Pass rv
		log_warn("aio error while dialing QUIC!");
		goto error;
	}

	// Connection was established. Nice. Then. Create the main quic stream.
	rv = msquic_strm_open(d->qconn, d);

error:
	d->currcon = NULL;
	if (rv != 0) {
		log_warn("error in openning QUIC stream %d", rv);
		c->dial_aio = NULL;

		nni_aio_set_prov_data(aio, NULL);
		nni_aio_list_remove(aio);
	}

	nni_mtx_unlock(&d->mtx);

	if (rv != 0) {
		nng_stream_close(&c->stream);
		// Decement reference of dialer
		nng_stream_free(&c->stream);
		nni_aio_finish_error(aio, rv);
	}
}

static const int flags_main_stream = QUIC_MAIN_STREAM;
static const int flags_sub_stream  = QUIC_SUB_STREAM;

// Dial to the `url`. Finish `aio` when connected.
// For quic. If the connection is not established.
// This nng stream is linked to main quic stream.
// Or it will link to a sub quic stream.
void
nni_quic_dial(void *arg, const char *host, const char *port, nni_aio *aio)
{
	nni_quic_conn *  c;
	nni_quic_dialer *d = arg;
	int              cnt;
	bool             ismain = false;
	int              rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
	log_info("begin dialing!");
	nni_atomic_inc64(&d->ref);

	// Create a connection whenever dial. So it's okey. right?
	if ((rv = nni_msquic_quic_alloc(&c, d)) != 0) {
		nni_aio_finish_error(aio, rv);
		nni_msquic_quic_dialer_rele(d);
		return;
	}

	nni_mtx_lock(&d->mtx);
	if (d->closed) {
		rv = NNG_ECLOSED;
		goto error;
	}

	cnt = nni_atomic_get64(&d->ref);

	ismain = (cnt == 2 ? true : false);

	nni_aio_set_output(aio, 1,
	        (void *)(ismain ? &flags_main_stream : &flags_sub_stream));

	// XXX Which should be uncommented when debug is finished
	//if (d->enable_mltstrm == false && ismain == false) {
	//	log_error("Please enable multistream first");
	//	rv = NNG_EINVAL;
	//	goto error;
	//}

	if ((rv = nni_aio_schedule(aio, quic_dialer_strm_cancel, d)) != 0) {
		goto error;
	}

	// pass c to the dialer for getting it in msquic_strm_open
	d->currcon = c;
	// set c to provdata. Which is helpful for quic dialer close.
	nni_aio_set_prov_data(aio, c);
	c->dial_aio = aio;

	if (ismain) {
		// Make a quic connection to the url.
		// Create stream after connection is established.
		if (nni_aio_busy(d->qconaio)) {
			rv = NNG_EBUSY;
			goto error;
		}

		if (nni_aio_begin(d->qconaio) != 0) {
			rv = NNG_ECLOSED;
			goto error;
		}

		if ((rv = nni_aio_schedule(d->qconaio, quic_dialer_cancel, d)) != 0) {
			// FIX quic dialer allows dialing multiple connections in parallel
			// however here we only has one qconaio?
			rv = NNG_ECANCELED;
			goto error;
		}

		if (0 != (rv = msquic_conn_open(host, port, d))) {
			log_warn("QUIC dialing failed! %d", rv);
			goto error;
		}
	} else {
		if ((rv = msquic_strm_open(d->qconn, d)) != 0) {
			rv = NNG_ECLOSED;
			goto error;
		}
	}

	nni_list_append(&d->connq, aio);

	nni_mtx_unlock(&d->mtx);

	return;

error:
	d->currcon = NULL;
	c->dial_aio = NULL;
	nni_aio_set_prov_data(aio, NULL);
	nni_mtx_unlock(&d->mtx);
	nng_stream_free(&c->stream);
	nni_aio_finish_error(aio, rv);
}

// Close the pending connection.
void
nni_quic_dialer_close(void *arg)
{
	nni_quic_dialer *d = arg;

	nni_mtx_lock(&d->mtx);
	if (!d->closed) {
		nni_aio *aio;
		d->closed = true;
		while ((aio = nni_list_first(&d->connq)) != NULL) {
			nni_quic_conn *c;
			nni_list_remove(&d->connq, aio);
			if ((c = nni_aio_get_prov_data(aio)) != NULL) {
				c->dial_aio = NULL;
				nni_aio_set_prov_data(aio, NULL);
				nng_stream_close(&c->stream);
				// We don't need to free the quic stream. Because it would be free
				// in QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE state.
				// QUIC_STREAM_EVENT_START_COMPLETE is not necessary to trigger
				// QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE.
				// nng_stream_free(&c->stream);
			}
			nni_aio_finish_error(aio, NNG_ECLOSED);
		}
	}
	nni_mtx_unlock(&d->mtx);
}

static void
quic_dialer_fini(nni_quic_dialer *d)
{
	// dialer is bound to QUIC connection
	log_info("connection %p fini", d->qconn);
	msquic_conn_close(d->qconn, 0);
	msquic_conn_fini(d->qconn);
	nni_aio_abort(d->qconaio, NNG_ECLOSED);
	nni_aio_wait(d->qconaio);
	nni_aio_free(d->qconaio);
	nni_mtx_fini(&d->mtx);
	NNI_FREE_STRUCT(d);
}

void
nni_quic_dialer_fini(nni_quic_dialer *d)
{
	nni_quic_dialer_close(d);
	nni_atomic_set_bool(&d->fini, true);
	nni_msquic_quic_dialer_rele(d);
}

void
nni_msquic_quic_dialer_rele(nni_quic_dialer *d)
{
	if (((nni_atomic_dec64_nv(&d->ref) != 0)) ||
	    (!nni_atomic_get_bool(&d->fini))) {
		return;
	}
	quic_dialer_fini(d);
}

/**************************** MsQuic Connection ****************************/

static void
quic_stream_cb(int events, void *arg, int rc)
{
	log_debug("[quic cb] start %d\n", events);
	nni_quic_conn   *c = arg;
	nni_quic_dialer *d;
	nni_aio         *aio;

	if (!c)
		return;

	d = c->dialer;

	switch (events) {
	case QUIC_STREAM_EVENT_SEND_COMPLETE:
		nni_mtx_lock(&c->mtx);
		if ((aio = nni_list_first(&c->writeq)) == NULL) {
			log_error("Aio lost after sending: conn %p", c);
			nni_mtx_unlock(&c->mtx);
			break;
		}
		nni_aio_list_remove(aio);
		QUIC_BUFFER *buf = nni_aio_get_input(aio, 0);
		free(buf);
		if (rc != 0)
			nni_aio_finish_error(aio, rc);
		else
			nni_aio_finish(aio, 0, nni_aio_count(aio));

		// Start next send only after finished the last send
		quic_stream_dowrite(c);

		nni_mtx_unlock(&c->mtx);
		break;
	case QUIC_STREAM_EVENT_START_COMPLETE:
		nni_mtx_lock(&d->mtx);
		if (c->dial_aio) {
			// For upper layer to get the stream handle
			nni_aio_set_output(c->dial_aio, 0, c);

			nni_aio_list_remove(c->dial_aio);
			nni_aio_finish(c->dial_aio, 0, 0);
			c->dial_aio = NULL;
		}

		nni_mtx_unlock(&d->mtx);
		break;
	// case QUIC_STREAM_EVENT_RECEIVE: // get a fin from stream
	// TODO Need more talk about those cases
	// case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
	// case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
	// case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
	case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
	// case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
		// Marked it as closed, prevent explicit shutdown
		c->closed = true;
		// It's the only place to free msquic stream
		msquic_strm_fini(c->qstrm);
		quic_stream_error(arg, NNG_ECONNSHUT);
		break;
	default:
		break;
	}
	log_debug("[quic cb] end\n");
}

static void
quic_stream_fini(void *arg)
{
	nni_quic_conn *c = arg;
	quic_stream_close(c);

	if (c->dialer) {
		nni_msquic_quic_dialer_rele(c->dialer);
	}
	NNI_FREE_STRUCT(c);
}

//static nni_reap_list quic_reap_list = {
//	.rl_offset = offsetof(nni_quic_conn, reap),
//	.rl_func   = quic_stream_fini,
//};
static void
quic_stream_free(void *arg)
{
	nni_quic_conn *c = arg;
	quic_stream_fini(c);
}

// Notify upper layer that something happened.
// Includes closed by peer or transport layer.
// Or get a FIN from quic stream.
static void
quic_stream_error(void *arg, int err)
{
	nni_quic_conn *c = arg;
	nni_aio *      aio;

	nni_mtx_lock(&c->mtx);
	// only close aio of this stream
	while ((aio = nni_list_first(&c->writeq)) != NULL) {
		nni_aio_list_remove(aio);
		nni_aio_finish_error(aio, err);
	}
	while ((aio = nni_list_first(&c->readq)) != NULL) {
		nni_aio_list_remove(aio);
		nni_aio_finish_error(aio, err);
	}
	nni_mtx_unlock(&c->mtx);
}

static void
quic_stream_close(void *arg)
{
	nni_quic_conn *c = arg;
	nni_mtx_lock(&c->mtx);
	if (c->closed != true) {
		c->closed = true;
		msquic_strm_close(c->qstrm);
	}
	nni_mtx_unlock(&c->mtx);
}

static void
quic_stream_cancel(nni_aio *aio, void *arg, int rv)
{
	nni_quic_conn *c = arg;

	nni_mtx_lock(&c->mtx);
	if (nni_aio_list_active(aio)) {
		nni_aio_list_remove(aio);
		nni_aio_finish_error(aio, rv);
	}
	nni_mtx_unlock(&c->mtx);
}

static void
quic_stream_recv(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&c->mtx);

	if ((rv = nni_aio_schedule(aio, quic_stream_cancel, c)) != 0) {
		nni_mtx_unlock(&c->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_aio_list_append(&c->readq, aio);

	// Receive start if there are only one aio in readq.
	if (nni_list_first(&c->readq) == aio) {
		// In msquic. To avoid repeated memory copies. We just enable
		// the receive. And doread in msquic_strm_cb. So there is
		// only one copy from msquic to nanonng.
		msquic_strm_recv_start(c->qstrm);
	}
	nni_mtx_unlock(&c->mtx);
}

static void
quic_stream_dowrite_prior(nni_quic_conn *c, nni_aio *aio)
{
	log_debug("[quic dowrite adv] start\n");
	int       rv;
	unsigned  naiov;
	nni_iov * aiov;
	size_t    n = 0;

	if (c->closed) {
		return;
	}

	nni_aio_get_iov(aio, &naiov, &aiov);

	QUIC_BUFFER *buf=(QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER)*naiov);
	for (uint8_t i = 0; i < naiov; ++i) {
		log_debug("buf%d sz %d", i, aiov[i].iov_len);
		buf[i].Buffer = aiov[i].iov_buf;
		buf[i].Length = aiov[i].iov_len;
		n += aiov[i].iov_len;
	}
	nni_aio_set_input(aio, 0, buf);

	if (QUIC_FAILED(rv = MsQuic->StreamSend(c->qstrm, buf,
	                naiov, QUIC_SEND_FLAG_NONE, aio))) {
		log_error("Failed in StreamSend, 0x%x!", rv);
		free(buf);
		nni_aio_finish_error(aio, NNG_ECANCELED);
		return;
	}

	nni_aio_bump_count(aio, n);
	log_debug("[quic dowrite adv] end");
}

static void
quic_stream_dowrite(nni_quic_conn *c)
{
	log_debug("[quic dowrite] start %p", c->qstrm);
	nni_aio *aio;
	int      rv;

	if (c->closed) {
		return;
	}

	while ((aio = nni_list_first(&c->writeq)) != NULL) {
		unsigned      naiov;
		nni_iov *     aiov;
		size_t        n = 0;

		nni_aio_get_iov(aio, &naiov, &aiov);
		if (naiov == 0)
			log_warn("A msg without content?");

		QUIC_BUFFER *buf=(QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER)*naiov);
		for (uint8_t i = 0; i < naiov; ++i) {
			log_debug("buf%d sz %d", i, aiov[i].iov_len);
			buf[i].Buffer = aiov[i].iov_buf;
			buf[i].Length = aiov[i].iov_len;
			n += aiov[i].iov_len;
		}
		nni_aio_set_input(aio, 0, buf);

		if (QUIC_FAILED(rv = MsQuic->StreamSend(c->qstrm, buf,
		                naiov, QUIC_SEND_FLAG_NONE, NULL))) {
			log_error("Failed in StreamSend, 0x%x!", rv);
			free(buf);
			// nni_aio_list_remove(aio);
			// nni_aio_finish_error(aio, NNG_ECLOSED);
			return;
		}

		nni_aio_bump_count(aio, n);

		break;
		// Different from tcp.
		// Here we just send one msg at once.
	}
}

static void
quic_stream_send(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	nni_mtx_lock(&c->mtx);

	// QUIC_HIGH_PRIOR_MSG Feature!
	int *flags = nni_aio_get_prov_data(aio);
	nni_aio_set_prov_data(aio, NULL);

	if (flags) {
		if (*flags & QUIC_HIGH_PRIOR_MSG) {
			quic_stream_dowrite_prior(c, aio);
			nni_mtx_unlock(&c->mtx);
			return;
		}
	}

	nni_mtx_unlock(&c->mtx);

	if (nni_aio_begin(aio) != 0) {
		return;
	}

	nni_mtx_lock(&c->mtx);

	if ((rv = nni_aio_schedule(aio, quic_stream_cancel, c)) != 0) {
		nni_mtx_unlock(&c->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}

	nni_aio_list_append(&c->writeq, aio);

	if (nni_list_first(&c->writeq) == aio) {
		quic_stream_dowrite(c);
		// In msquic. Write can be done at any time.
	}
	nni_mtx_unlock(&c->mtx);
}

static int
quic_stream_get(void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(buf);
	NNI_ARG_UNUSED(szp);
	NNI_ARG_UNUSED(t);
	return 0;

	// nni_quic_conn *c = arg;
	// return (nni_getopt(tcp_options, name, c, buf, szp, t));
}

static int
quic_stream_set(void *arg, const char *name, const void *buf, size_t sz, nni_type t)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(buf);
	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);
	return 0;

	// nni_quic_conn *c = arg;
	// return (nni_setopt(tcp_options, name, c, buf, sz, t));
}

int
nni_msquic_quic_alloc(nni_quic_conn **cp, nni_quic_dialer *d)
{
	nni_quic_conn *c;
	if ((c = NNI_ALLOC_STRUCT(c)) == NULL) {
		return (NNG_ENOMEM);
	}

	c->closed = false;
	c->dialer = d;

	nni_mtx_init(&c->mtx);
	nni_aio_list_init(&c->readq);
	nni_aio_list_init(&c->writeq);

	c->stream.s_free  = quic_stream_free;
	c->stream.s_close = quic_stream_close;
	c->stream.s_recv  = quic_stream_recv;
	c->stream.s_send  = quic_stream_send;
	c->stream.s_get   = quic_stream_get;
	c->stream.s_set   = quic_stream_set;

	*cp = c;
	return (0);
}

/***************************** MsQuic Bindings *****************************/

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK) QUIC_STATUS QUIC_API
msquic_connection_cb(_In_ HQUIC Connection, _In_opt_ void *Context,
	_Inout_ QUIC_CONNECTION_EVENT *Event)
{
	nni_quic_dialer *d = Context;
	HQUIC            qconn = Connection;
	QUIC_STATUS      rv;

	log_debug("msquic_connection_cb triggered! %d", Event->Type);
	switch (Event->Type) {
	case QUIC_CONNECTION_EVENT_CONNECTED:
		// The handshake has completed for the connection.
		// do not init any var here due to potential frequent reconnect
		log_info("[conn][%p] is Connected. Resumed Session %d", qconn,
		    Event->CONNECTED.SessionResumed);

		nni_aio_finish(d->qconaio, 0, 0);
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		// The connection has been shut down by the transport.
		// Generally, this is the expected way for the connection to
		// shut down with this protocol, since we let idle timeout kill
		// the connection.
		log_warn("[conn][%p] Shutdown by transport, 0x%x, Error Code %llu\n",
		    qconn, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status,
		    (unsigned long long)
		        Event->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode);

		switch (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status) {
		case QUIC_STATUS_CONNECTION_IDLE:
			log_warn("[conn][%p] Connection shutdown on idle.\n", qconn);
			break;
		case QUIC_STATUS_CONNECTION_TIMEOUT:
			log_warn("[conn][%p] Shutdown on CONNECTION_TIMEOUT.\n", qconn);
			break;
		case QUIC_STATUS_UNREACHABLE:
			log_warn("[conn][%p] Host unreachable.\n", qconn);
			break;
		default:
			log_warn("No network availiable. Random errcode is used");
			break;
		}
		if (d->reason_code == 0)
            	d->reason_code = SERVER_UNAVAILABLE;
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		// The connection was explicitly shut down by the peer.
		log_warn("[conn][%p] "
		         "QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER, "
		         "0x%llu\n",
		    qconn,
		    (unsigned long long)
		        Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		if (d->reason_code == 0)
			d->reason_code = SERVER_SHUTTING_DOWN;
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		// The connection has completed the shutdown process and is
		// ready to be safely cleaned up.
		log_info("[conn][%p] QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: All done\n\n", qconn);
		nni_mtx_lock(&d->mtx);
		if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
			// explicitly shutdon on protocol layer.
			MsQuic->ConnectionClose(qconn);
		}
		// reconnect here
		nni_mtx_unlock(&d->mtx);
		nni_aio_finish_error(d->qconaio, d->reason_code);
		break;
	case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
		// A resumption ticket (also called New Session Ticket or NST)
		// was received from the server.
		log_warn("[conn][%p] Resumption ticket received (%u bytes):\n",
		    Connection, Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
		if (d->enable_0rtt == false) {
			log_warn("[conn][%p] Ignore ticket due to turn off the 0RTT");
			break;
		}
		d->rticket_sz = Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength;
		if (d->rticket_sz > 4096) {
			log_error("rticket is too long.");
			break;
		}
		memcpy(d->rticket, Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicket,
		        Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
		break;
	case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
		log_info("QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED");
		// TODO Using mbedtls APIs to verify
		/*
		 * TODO
		 * Does MsQuic ensure the connected event will happen after
		 * peer_certificate_received event.?
		 */
		if (QUIC_FAILED(rv = verify_peer_cert_tls(
				Event->PEER_CERTIFICATE_RECEIVED.Certificate,
				Event->PEER_CERTIFICATE_RECEIVED.Chain, d->ca))) {
			log_error("[conn][%p] Invalid certificate file received from the peer", qconn);
			return rv;
		}
		break;
	case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
		log_info("QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED");
		break;
	case QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE:
		log_info("QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE");
		break;
	case QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED:
		log_info("QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED");
		break;
	default:
		log_warn("Unknown event type %d!", Event->Type);
		break;
	}
	return QUIC_STATUS_SUCCESS;
}

// The clients's callback for stream events from MsQuic.
// New recv cb of quic transport
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK) QUIC_STATUS QUIC_API
msquic_strm_cb(_In_ HQUIC stream, _In_opt_ void *Context,
	_Inout_ QUIC_STREAM_EVENT *Event)
{
	nni_quic_conn *c = Context;
	nni_aio       *aio;
	nni_iov *      aiov;
	unsigned       naiov;
	uint32_t       rlen, rlen2, rpos;
	uint8_t       *rbuf;
	uint32_t       count;
	bool           canceled;

	log_debug("quic_strm_cb triggered! %d conn %p strm %p", Event->Type, c, stream);
	switch (Event->Type) {
	case QUIC_STREAM_EVENT_SEND_COMPLETE:
		log_debug("QUIC_STREAM_EVENT_SEND_COMPLETE!");
		canceled = false;
		if (Event->SEND_COMPLETE.Canceled) {
			log_warn("[strm][%p] Data sent Canceled: %d",
			    stream, Event->SEND_COMPLETE.Canceled);
			canceled = true;
		}
		// Priority msg send
		if ((aio = Event->SEND_COMPLETE.ClientContext) != NULL) {
			QUIC_BUFFER *buf = nni_aio_get_input(aio, 0);
			free(buf);
			Event->SEND_COMPLETE.ClientContext = NULL;
			nni_msg *msg = nni_aio_get_msg(aio);
			nni_mqtt_packet_type t = nni_mqtt_msg_get_packet_type(msg);
			nni_msg_free(msg);
			nni_aio_set_msg(aio, NULL);
			if (canceled)
				nni_aio_finish_error(aio, NNG_ECANCELED);
			else
				nni_aio_finish(aio, 0, nni_aio_count(aio));
			break;
		}
		// Ordinary send
		if (canceled)
			quic_stream_cb(QUIC_STREAM_EVENT_SEND_COMPLETE, c, NNG_ECANCELED);
		else
			quic_stream_cb(QUIC_STREAM_EVENT_SEND_COMPLETE, c, 0);
		break;
	case QUIC_STREAM_EVENT_RECEIVE:
		// Data was received from the peer on the stream.
		count = Event->RECEIVE.BufferCount;

		log_debug("[strm][%p] Data received Flag: %d", stream, Event->RECEIVE.Flags);

		if (Event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) {
			if (c->reason_code == 0)
				c->reason_code = CLIENT_IDENTIFIER_NOT_VALID;
			log_warn("FIN received in QUIC stream");
			break;
		}

		nni_mtx_lock(&c->mtx);
		if (c->closed) {
			// Actively closed the quic stream by upper layer. So ignore.
			nni_mtx_unlock(&c->mtx);
			return QUIC_STATUS_PENDING;
		}
		// Get all the buffers in quic stream
		if (count == 0) {
			nni_mtx_unlock(&c->mtx);
			return QUIC_STATUS_PENDING;
		}

		rbuf = Event->RECEIVE.Buffers[0].Buffer;
		rlen = Event->RECEIVE.Buffers[0].Length;

		rpos = 0;
		while ((aio = nni_list_first(&c->readq)) != NULL) {
			nni_aio_get_iov(aio, &naiov, &aiov);
			int n = 0;
			for (uint8_t i=0; i<naiov; ++i) {
				if (aiov[i].iov_len == 0)
					continue;
				rlen2 = rlen - rpos; // remain
				if (rlen2 == 0)
					break;
				if (rlen2 >= aiov[i].iov_len) {
					memcpy(aiov[i].iov_buf, rbuf+rpos, aiov[i].iov_len);
					rpos += aiov[i].iov_len;
					n += aiov[i].iov_len;
				} else {
					memcpy(aiov[i].iov_buf, rbuf+rpos, rlen2);
					rpos += rlen2;
					n += rlen2;
				}
			}
			if (n == 0) { // rbuf run out
				break;
			}
			nni_aio_bump_count(aio, n);

			// We completed the entire operation on this aio.
			nni_aio_list_remove(aio);
			nni_aio_finish(aio, 0, nni_aio_count(aio));

			// Go back to start of loop to see if there is another
			// aio ready for us to process.
		}

		MsQuic->StreamReceiveComplete(c->qstrm, rpos);
		nni_mtx_unlock(&c->mtx);

		return QUIC_STATUS_PENDING;
	case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
		// The peer gracefully shut down its send direction of the
		// stream.
		log_warn("[strm][%p] PEER_SEND_ABORTED errorcode %llu\n", stream,
		    (unsigned long long) Event->PEER_SEND_ABORTED.ErrorCode);
		if (c->reason_code == 0)
			c->reason_code = SERVER_SHUTTING_DOWN;

		quic_stream_cb(QUIC_STREAM_EVENT_PEER_SEND_ABORTED, c, 0);
		break;
	case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
		// The peer aborted its send direction of the stream.
		log_warn("[strm][%p] Peer send shut down\n", stream);
		MsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
		quic_stream_cb(QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN, c, 0);
		break;
	case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
		log_warn("[strm][%p] QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE.", stream);
		break;

	case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
		// Both directions of the stream have been shut down and MsQuic
		// is done with the stream. It can now be safely cleaned up.
		log_warn("[strm][%p] QUIC_STREAM_EVENT shutdown: All done.",
		    stream);
		log_info("close stream with Error Code: %llu",
		    (unsigned long long)
		        Event->SHUTDOWN_COMPLETE.ConnectionErrorCode);
		quic_stream_cb(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE, c, 0);
		break;
	case QUIC_STREAM_EVENT_START_COMPLETE:
		log_info(
		    "QUIC_STREAM_EVENT_START_COMPLETE [%p] ID: %ld Status: %d",
		    stream, Event->START_COMPLETE.ID,
		    Event->START_COMPLETE.Status);
		if (!Event->START_COMPLETE.PeerAccepted) {
			log_warn("Peer refused");
			quic_stream_cb(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE, c, 0);
			break;
		}

		quic_stream_cb(QUIC_STREAM_EVENT_START_COMPLETE, c, 0);
		break;
	case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE:
		log_info("QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE");
		break;
	case QUIC_STREAM_EVENT_PEER_ACCEPTED:
		log_info("QUIC_STREAM_EVENT_PEER_ACCEPTED");
		break;
	case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
		// The peer has requested that we stop sending. Close abortively.
		log_warn("[strm][%p] Peer RECEIVE aborted\n", stream);
		log_warn("QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED Error Code: %llu",
		    (unsigned long long) Event->PEER_RECEIVE_ABORTED.ErrorCode);

		quic_stream_cb(QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED, c, 0);
		break;

	default:
		log_warn("Unknown Event Type %d", Event->Type);
		break;
	}
	return QUIC_STATUS_SUCCESS;
}

static int is_msquic_inited = 0;

static void
msquic_close()
{
	if (MsQuic != NULL) {
		if (configuration != NULL) {
			MsQuic->ConfigurationClose(configuration);
		}
		if (registration != NULL) {
			// This will block until all outstanding child objects
			// have been closed.
			MsQuic->RegistrationClose(registration);
		}
		MsQuicClose(MsQuic);
		is_msquic_inited = 0;
	}
}

static int
msquic_open()
{
	if (is_msquic_inited == 1)
		return 0;

	QUIC_STATUS rv = QUIC_STATUS_SUCCESS;
	// only Open MsQUIC lib once, otherwise cause memleak
	if (MsQuic == NULL)
		if (QUIC_FAILED(rv = MsQuicOpen2(&MsQuic))) {
			log_error("MsQuicOpen2 failed, 0x%x!\n", rv);
			goto error;
		}

	// Create a registration for the app's connections.
	rv = MsQuic->RegistrationOpen(&quic_reg_config, &registration);
	if (QUIC_FAILED(rv)) {
		log_error("RegistrationOpen failed, 0x%x!\n", rv);
		goto error;
	}

	is_msquic_inited = 1;
	log_info("Msquic is enabled");
	return 0;

error:
	msquic_close();
	return -1;
}

// Helper function to load a client configuration.
static BOOLEAN
msquic_load_config(QUIC_SETTINGS *settings, nni_quic_dialer *d)
{
	QUIC_CREDENTIAL_CONFIG CredConfig;

	// Allocate/initialize the configuration object, with the configured
	// ALPN and settings.
	QUIC_STATUS rv = QUIC_STATUS_SUCCESS;
	if (QUIC_FAILED(rv = MsQuic->ConfigurationOpen(registration,
	    &quic_alpn, 1, settings, sizeof(*settings), NULL, &configuration))) {
		log_error("ConfigurationOpen failed, 0x%x!\n", rv);
		return FALSE;
	}

	// TLS/SSL
	memset(&CredConfig, 0, sizeof(CredConfig));
	// Unsecure by default
	CredConfig.Type  = QUIC_CREDENTIAL_TYPE_NONE;
	// CredConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_USE_PORTABLE_CERTIFICATES;
	CredConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;

	// Start tls need cacert and key at least
	if (!d->cacert || !d->key) {
		CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
		log_warn("No quic TLS/SSL credentials (cacert and key) was specified.");
		goto settls;
	}

	char *cert_path = d->cacert;
	char *key_path  = d->key;
	char *password  = d->password;
	BOOLEAN verify = (d->verify_peer == true ? 1 : 0);
	BOOLEAN has_ca_cert = (d->ca != NULL ? 1 : 0);

	if (password) {
		QUIC_CERTIFICATE_FILE_PROTECTED *CertFile =
		    (QUIC_CERTIFICATE_FILE_PROTECTED *) malloc(sizeof(QUIC_CERTIFICATE_FILE_PROTECTED));
		CertFile->CertificateFile           = cert_path;
		CertFile->PrivateKeyFile            = key_path;
		CertFile->PrivateKeyPassword        = password;
		CredConfig.CertificateFileProtected = CertFile;
		CredConfig.Type =
		    QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE_PROTECTED;
	} else {
		QUIC_CERTIFICATE_FILE *CertFile =
		    (QUIC_CERTIFICATE_FILE_PROTECTED *) malloc(sizeof(QUIC_CERTIFICATE_FILE_PROTECTED));
		CertFile->CertificateFile  = cert_path;
		CertFile->PrivateKeyFile   = key_path;
		CredConfig.CertificateFile = CertFile;
		CredConfig.Type =
		    QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
	}

	if (!verify) {
		CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
	} else if (has_ca_cert) {
		// Do own validation instead against provided ca certs in cacertfile
		CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED;
		CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
	}

	CredConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
	CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED;

settls:
	// Loads the TLS credential part of the configuration. This is required
	// even on client side, to indicate if a certificate is required or
	// not.
	if (QUIC_FAILED(rv = MsQuic->ConfigurationLoadCredential(
	                    configuration, &CredConfig))) {
		log_error("Configuration Load Credential failed, 0x%x!\n", rv);
		if (CredConfig.CertificateFile != NULL)
			nng_free(CredConfig.CertificateFile, sizeof(QUIC_CERTIFICATE_FILE_PROTECTED));
		return FALSE;
	}
	if (CredConfig.CertificateFile != NULL)
		nng_free(CredConfig.CertificateFile, sizeof(QUIC_CERTIFICATE_FILE_PROTECTED));

	return TRUE;
}

static int
msquic_conn_open(const char *host, const char *port, nni_quic_dialer *d)
{
	QUIC_STATUS  rv;
	HQUIC        conn = NULL;

	if (0 != msquic_open()) {
		// so... close the quic connection
		return (NNG_ESYSERR);
	}

	if (TRUE != msquic_load_config(&d->settings, d)) {
		// error in configuration so... close the quic connection
		return (NNG_EINVAL);
	}

	// Allocate a new connection object.
	if (QUIC_FAILED(rv = MsQuic->ConnectionOpen(registration,
	        msquic_connection_cb, (void *)d, &conn))) {
		log_error("Failed in Quic ConnectionOpen, 0x%x!", rv);
		goto error;
	}

	// TODO CA 0RTT Windows campatible interface index

	log_info("Quic connecting... %s:%s", host, port);

	if (QUIC_FAILED(rv = MsQuic->ConnectionStart(conn, configuration,
	        QUIC_ADDRESS_FAMILY_UNSPEC, host, atoi(port)))) {
		log_error("Failed in ConnectionStart, 0x%x!", rv);
		goto error;
	}

	d->qconn = conn;

	return 0;
error:

	return (NNG_ECONNREFUSED);
}

static void
msquic_conn_close(HQUIC qconn, int rv)
{
	MsQuic->ConnectionShutdown(qconn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, (QUIC_UINT62)rv);
}

static void
msquic_conn_fini(HQUIC qconn)
{
	MsQuic->ConnectionClose(qconn);
}

static int
msquic_strm_open(HQUIC qconn, nni_quic_dialer *d)
{
	HQUIC          strm = NULL;
	QUIC_STATUS    rv;
	nni_quic_conn *c;

	log_debug("[strm][%p] Starting...", strm);

	c = d->currcon;

	rv = MsQuic->StreamOpen(qconn, QUIC_STREAM_OPEN_FLAG_NONE,
	        msquic_strm_cb, (void *)c, &strm);
	if (QUIC_FAILED(rv)) {
		log_error("StreamOpen failed, 0x%x!\n", rv);
		goto error;
	}

	if (d->priority != -1) {
		printf("Ready to create a quic stream with priority: %d\n", d->priority);
		MsQuic->SetParam(strm, QUIC_PARAM_STREAM_PRIORITY, sizeof(int), &d->priority);
	}

	rv = MsQuic->StreamStart(strm, QUIC_STREAM_START_FLAG_NONE);
	if (QUIC_FAILED(rv)) {
		log_error("StreamStart failed, 0x%x!\n", rv);
		MsQuic->StreamClose(strm);
		goto error;
	}

	// Not ready for receiving
	MsQuic->StreamReceiveSetEnabled(strm, FALSE);
	c->qstrm = strm;

	log_debug("[strm][%p] Done...\n", strm);

	return 0;
error:
	return (NNG_ECLOSED);
}

static void
msquic_strm_close(HQUIC qstrm)
{
	log_info("stream %p shutdown", qstrm);
	MsQuic->StreamShutdown(
	    qstrm, QUIC_STREAM_SHUTDOWN_FLAG_ABORT | QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE, NNG_ECONNSHUT);
}

static void
msquic_strm_fini(HQUIC qstrm)
{
	log_info("stream %p fini", qstrm);
	MsQuic->StreamClose(qstrm);
}

static void
msquic_strm_recv_start(HQUIC qstrm)
{
	MsQuic->StreamReceiveSetEnabled(qstrm, TRUE);
}
