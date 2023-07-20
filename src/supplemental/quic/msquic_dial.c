//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// #if defined(NNG_ENABLE_QUIC) // && defined(NNG_QUIC_MSQUIC)

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

struct nni_quic_dialer {
	nni_aio                *qconaio; // for quic connection
	nni_quic_conn          *currcon;
	nni_list                connq;   // pending connections/quic streams
	bool                    closed;
	bool                    nodelay;
	bool                    keepalive;
	struct sockaddr_storage src;
	size_t                  srclen;
	nni_mtx                 mtx;
	nni_atomic_u64          ref;
	nni_atomic_bool         fini;

	// MsQuic
	HQUIC                   qconn; // quic connection
	bool                    enable_0rtt;
	uint8_t                 reason_code;
	// ResumptionTicket
	char *                  rticket;
	uint16_t                rticket_sz;
	// CertificateFile
	char *                  cacert;
};

struct nni_quic_conn {
	nng_stream      stream;
	nni_list        readq;
	nni_list        writeq;
	bool            closed;
	nni_mtx         mtx;
	nni_aio *       dial_aio;
	nni_aio *       qstrmaio;
	nni_quic_dialer *dialer;
	nni_reap_node   reap;

	// MsQuic
	HQUIC           qstrm; // quic stream
	// A buffer for cache msg from msquic
	uint32_t        rrcap;
	uint32_t        rrlen;
	uint32_t        rrpos;
	char *          rrbuf;
	uint8_t         reason_code;
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
static int  msquic_connect(const char *host, const char *port, nni_quic_dialer *d);
static int  msquic_strm_connect(HQUIC qconn, nni_quic_dialer *d);
static void msquic_strm_recv_start(HQUIC qstrm);

static void quic_dialer_cb();

int
nni_quic_dialer_init(void **argp)
{
	nni_quic_dialer *d;

	if ((d = NNI_ALLOC_STRUCT(d)) == NULL) {
		return (NNG_ENOMEM);
	}

	nni_mtx_init(&d->mtx);
	d->closed = false;
	d->currcon = NULL;
	nni_aio_alloc(&d->qconaio, quic_dialer_cb, (void *)d);
	nni_aio_list_init(&d->connq);
	nni_atomic_init_bool(&d->fini);
	nni_atomic_init64(&d->ref);
	nni_atomic_inc64(&d->ref);

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

	nni_mtx_lock(&d->mtx);
	d->currcon = NULL;
	nni_mtx_unlock(&d->mtx);

	nni_aio_finish_error(aio, rv);
}

static void
quic_dialer_cb(void *arg)
{
	nni_quic_dialer *d = arg;
	int              rv;

	if (nni_aio_result(d->qconaio) != 0) {
		return;
	}

	// Connection was established. Nice. Then. Create the main quic stream.
	rv = msquic_strm_connect(d->qconn, d);
}

// Dial to the `url`. Finish `aio` when connected.
// For quic. If the connection is not established.
// This nng stream is linked to main quic stream.
// Or it will link to a sub quic stream.
void
nni_quic_dial(void *arg, const char *host, const char *port, nni_aio *aio)
{
	nni_quic_conn *  c;
	nni_quic_dialer *d = arg;
	bool             ismain = false;
	int              rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}

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

	// TODO check if there are any quic streams to the url.
	ismain = true;

	if ((rv = nni_aio_schedule(aio, quic_dialer_strm_cancel, d)) != 0) {
		goto error;
	}

	// pass c to the dialer for getting it in msquic_strm_connect
	d->currcon = c;

	if (ismain) {
		// Make a quic connection to the url.
		// Create stream after connection is established.
		if ((rv = nni_aio_schedule(d->qconaio, quic_dialer_cancel, d)) != 0) {
			rv = NNG_ECLOSED;
			goto error;
		}

		if (0 != msquic_connect(host, port, d)) {
			rv = NNG_ECLOSED;
			goto error;
		}
	} else {
		if ((rv = msquic_strm_connect(d->qconn, d)) != 0) {
			rv = NNG_ECLOSED;
			goto error;
		}
	}

	nni_list_append(d->connq, aio);

	nni_mtx_unlock(&d->mtx);

	return;

error:
	d->currcon = NULL;
	nni_mtx_unlock(&d->mtx);
	nni_aio_finish_error(aio, rv);
}

// Close the pending connection.
void
nni_quic_dialer_close(void *arg)
{
	nni_quic_dialer *d = arg;
	NNI_ARG_UNUSED(d);
}

static void
quic_dialer_fini(nni_quic_dialer *d)
{
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
quic_cb(unsigned events, void *arg)
{
}

static void
quic_free(void *arg)
{
	nni_quic_conn *c = arg;
	// nni_reap(&tcp_reap_list, c);
}

static void
quic_close2(void *arg)
{
	nni_quic_conn *c = arg;
}

static void
quic_doread(nni_quic_conn *c)
{
	nni_aio *aio;
	int      fd;

	if (c->closed) {
		return;
	}

	/*
	while ((aio = nni_list_first(&c->readq)) != NULL) {
		unsigned     i;
		int          n;
		int          niov;
		unsigned     naiov;
		nni_iov *    aiov;
		struct iovec iovec[16];

		nni_aio_get_iov(aio, &naiov, &aiov);
		if (naiov > NNI_NUM_ELEMENTS(iovec)) {
			nni_aio_list_remove(aio);
			nni_aio_finish_error(aio, NNG_EINVAL);
			continue;
		}
		for (niov = 0, i = 0; i < naiov; i++) {
			if (aiov[i].iov_len != 0) {
				iovec[niov].iov_len  = aiov[i].iov_len;
				iovec[niov].iov_base = aiov[i].iov_buf;
				niov++;
			}
		}

		if ((n = readv(fd, iovec, niov)) < 0) {
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
				return;
			default:
				nni_aio_list_remove(aio);
				nni_aio_finish_error(
				    aio, nni_plat_errno(errno));
				return;
			}
		}

		if (n == 0) {
			// No bytes indicates a closed descriptor.
			// This implicitly completes this (all!) aio.
			nni_aio_list_remove(aio);
			nni_aio_finish_error(aio, NNG_ECONNSHUT);
			continue;
		}

		nni_aio_bump_count(aio, n);

		// We completed the entire operation on this aio.
		nni_aio_list_remove(aio);
		nni_aio_finish(aio, 0, nni_aio_count(aio));

		// Go back to start of loop to see if there is another
		// aio ready for us to process.
	}
	*/
}

static void
quic_cancel(nni_aio *aio, void *arg, int rv)
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
quic_recv(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&c->mtx);

	if ((rv = nni_aio_schedule(aio, quic_cancel, c)) != 0) {
		nni_mtx_unlock(&c->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_aio_list_append(&c->readq, aio);

	// If we are only job on the list, go ahead and try to do an
	// immediate transfer. This allows for faster completions in
	// many cases.  We also need not arm a list if it was already
	// armed.
	if (nni_list_first(&c->readq) == aio) {
		quic_doread(c);
		// If we are still the first thing on the list, that
		// means we didn't finish the job, so arm the poller to
		// complete us.
		msquic_strm_recv_start(c->qstrm);
	}
	nni_mtx_unlock(&c->mtx);
}

static void
quic_dowrite(nni_quic_conn *c)
{
	nni_aio *aio;
	int      fd;

	if (c->closed) {
		return;
	}

	/*
	while ((aio = nni_list_first(&c->writeq)) != NULL) {
		unsigned      i;
		int           n;
		int           niov;
		unsigned      naiov;
		nni_iov *     aiov;
		struct msghdr hdr;
		struct iovec  iovec[16];

		memset(&hdr, 0, sizeof(hdr));
		nni_aio_get_iov(aio, &naiov, &aiov);

		if (naiov > NNI_NUM_ELEMENTS(iovec)) {
			nni_aio_list_remove(aio);
			nni_aio_finish_error(aio, NNG_EINVAL);
			continue;
		}

		for (niov = 0, i = 0; i < naiov; i++) {
			if (aiov[i].iov_len > 0) {
				iovec[niov].iov_len  = aiov[i].iov_len;
				iovec[niov].iov_base = aiov[i].iov_buf;
				niov++;
			}
		}

		hdr.msg_iovlen = niov;
		hdr.msg_iov    = iovec;

		if ((n = sendmsg(fd, &hdr, MSG_NOSIGNAL)) < 0) {
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
#ifdef EWOULDBLOCK
#if EWOULDBLOCK != EAGAIN
			case EWOULDBLOCK:
#endif
#endif
				return;
			default:
				nni_aio_list_remove(aio);
				nni_aio_finish_error(
				    aio, nni_plat_errno(errno));
				return;
			}
		}

		nni_aio_bump_count(aio, n);
		// We completed the entire operation on this aio.
		// (Sendmsg never returns a partial result.)
		nni_aio_list_remove(aio);
		nni_aio_finish(aio, 0, nni_aio_count(aio));

		// Go back to start of loop to see if there is another
		// aio ready for us to process.
	}
	*/
}

static void
quic_send(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}

	if ((rv = nni_aio_schedule(aio, quic_cancel, c)) != 0) {
		nni_mtx_unlock(&c->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_aio_list_append(&c->writeq, aio);

	if (nni_list_first(&c->writeq) == aio) {
		quic_dowrite(c);
		// In msquic. Write can be done at any time.
	}
	nni_mtx_unlock(&c->mtx);

}

static int
quic_get(void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	nni_quic_conn *c = arg;
	// return (nni_getopt(tcp_options, name, c, buf, szp, t));
}

static int
quic_set(void *arg, const char *name, const void *buf, size_t sz, nni_type t)
{
	nni_quic_conn *c = arg;
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

	c->stream.s_free  = quic_free;
	c->stream.s_close = quic_close2;
	c->stream.s_recv  = quic_recv;
	c->stream.s_send  = quic_send;
	c->stream.s_get   = quic_get;
	c->stream.s_set   = quic_set;

	*cp = c;
	return (0);
}

void
nni_msquic_quic_init(nni_quic_conn *c)
{
}

void
nni_msquic_quic_start(nni_quic_conn *c, int nodelay, int keepalive)
{
	// Configure the initial quic connection.
	// ...TODO

	// nni_posix_pfd_set_cb(c->pfd, quic_cb, c);
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
		if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status ==
		    QUIC_STATUS_CONNECTION_IDLE) {
			log_warn("[conn][%p] Connection shutdown on idle.\n", qconn);
		} else if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status ==
		    QUIC_STATUS_CONNECTION_TIMEOUT) {
			log_warn("[conn][%p] Shutdown on CONNECTION_TIMEOUT.\n", qconn);
		}
		log_warn("[conn][%p] Shutdown by transport, 0x%x, Error Code %llu\n",
		    qconn, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status,
		    (unsigned long long)
		        Event->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode);
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

		// TODO Decrement refcnt of all quic streams in this dialer

		nni_mtx_unlock(&d->mtx);
		/*
		if (d->closed == true) {
			nni_sock_rele(qsock->sock);
		} else {
			nni_aio_finish(&qsock->close_aio, 0, 0);
		}
		if (qstrm->rtt0_enable) {
			// No rticket
			log_warn("reconnect failed due to no resumption ticket.\n");
			quic_strm_fini(qstrm);
			nng_free(qstrm, sizeof(quic_strm_t));
		}
		*/

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
		/*
		if (QUIC_FAILED(rv = verify_peer_cert_tls(
				Event->PEER_CERTIFICATE_RECEIVED.Certificate,
				Event->PEER_CERTIFICATE_RECEIVED.Chain, d->cacert))) {
			log_error("[conn][%p] Invalid certificate file received from the peer");
			return rv;
		}
		*/
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

	uint32_t rlen;
	uint8_t *rbuf;
	nni_msg *smsg;
	nni_aio *aio;
	uint32_t count;
	uint64_t total;

	log_debug("quic_strm_cb triggered! %d", Event->Type);
	switch (Event->Type) {
	case QUIC_STREAM_EVENT_SEND_COMPLETE:
		// A previous StreamSend call has completed, and the context is
		// being returned back to the app.
		log_debug("QUIC_STREAM_EVENT_SEND_COMPLETE!");
		if (Event->SEND_COMPLETE.Canceled) {
			log_warn("[strm][%p] Data sent Canceled: %d",
					 stream, Event->SEND_COMPLETE.Canceled);
		}
		break;
	case QUIC_STREAM_EVENT_RECEIVE:
		// Data was received from the peer on the stream.
		count = Event->RECEIVE.BufferCount;
		total = 0;

		log_debug("[strm][%p] Data received Flag: %d", stream, Event->RECEIVE.Flags);

		if (Event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) {
			if (c->reason_code == 0)
				c->reason_code = CLIENT_IDENTIFIER_NOT_VALID;
			log_warn("FIN received in QUIC stream");
			break;
		}

		for (uint32_t i=0; i<count; ++i) {
			rlen = Event->RECEIVE.Buffers[i].Length;
			if (rlen <= 0)
				continue;
			total += rlen;
		}

		nni_mtx_lock(&c->mtx);
		// Get all the buffers in quic stream
		if (count == 0 || total == 0) {
			nni_mtx_unlock(&c->mtx);
			return QUIC_STATUS_PENDING;
		}

		if (total > c->rrcap - c->rrlen - c->rrpos) {
			c->rrbuf = realloc(c->rrbuf, total + c->rrlen + c->rrpos);
			c->rrcap = total + c->rrlen + c->rrpos;
		}

		for (uint32_t i=0; i<count; ++i) {
			rbuf = Event->RECEIVE.Buffers[i].Buffer;
			rlen = Event->RECEIVE.Buffers[i].Length;

			log_debug("Body is [%d]-[0x%x 0x%x].", rlen, *(rbuf), *(rbuf + 1));

			if (rlen <= 0)
				continue;

			// Copy data from quic stream to rrbuf
			memcpy(c->rrbuf + c->rrpos + (int)c->rrlen, rbuf, rlen);
			c->rrlen += rlen;
		}

		MsQuic->StreamReceiveComplete(c->qstrm, total);
		nni_mtx_unlock(&c->mtx);
		// Expose RECEIVE event
		// Finish aio from upper layer would be done in quic_cb.
		quic_cb(QUIC_STREAM_EVENT_RECEIVE, c);
		log_debug("stream cb over\n");

		return QUIC_STATUS_PENDING;
	case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
		// The peer gracefully shut down its send direction of the
		// stream.
		log_warn("[strm][%p] Peer SEND aborted\n", stream);
		log_info("PEER_SEND_ABORTED Error Code: %llu",
		    (unsigned long long) Event->PEER_SEND_ABORTED.ErrorCode);
		if (c->reason_code == 0)
			c->reason_code = SERVER_SHUTTING_DOWN;
		break;
	case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
		// The peer aborted its send direction of the stream.
		log_warn("[strm][%p] Peer send shut down\n", stream);
		MsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
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
		c->closed = true;
		quic_cb(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE, c);
		break;
	case QUIC_STREAM_EVENT_START_COMPLETE:
		log_info(
		    "QUIC_STREAM_EVENT_START_COMPLETE [%p] ID: %ld Status: %d",
		    stream, Event->START_COMPLETE.ID,
		    Event->START_COMPLETE.Status);
		if (!Event->START_COMPLETE.PeerAccepted) {
			log_warn("Peer refused");
			nni_aio_finish_error(c->qstrmaio, NNG_ECONNREFUSED);
			break;
		}

		nni_aio_finish(c->qstrmaio, 0, 0);
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

static int
msquic_connect(const char *host, const char *port, nni_quic_dialer *d)
{
	QUIC_STATUS  rv;
	HQUIC        conn = NULL;

	if (0 != msquic_open()) {
		// so... close the quic connection
		return (NNG_ECLOSED);
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

	return (NNG_ECLOSED);
}

static int
msquic_strm_connect(HQUIC qconn, nni_quic_dialer *d)
{
	HQUIC          strm = NULL;
	QUIC_STATUS    rv;
	nni_quic_conn *c;

	nni_mtx_lock(&d->mtx);

	log_debug("[strm][%p] Starting...", strm);

	c = d->currcon;
	d->currcon = NULL;

	rv = MsQuic->StreamOpen(qconn, QUIC_STREAM_OPEN_FLAG_NONE,
	        msquic_strm_cb, (void *)c, &strm);
	if (QUIC_FAILED(rv)) {
		log_error("StreamOpen failed, 0x%x!\n", rv);
		goto error;
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

	nni_mtx_unlock(&d->mtx);

	return 0;
error:

	return (NNG_ECLOSED);
}

static void
msquic_strm_recv_start(HQUIC qstrm)
{
	MsQuic->StreamReceiveSetEnabled(qstrm, TRUE);
}
