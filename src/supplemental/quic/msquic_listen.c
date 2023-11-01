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
#include "quic_private.h"
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

static void msquic_listener_fini(HQUIC ql);
static void msquic_listener_stop(HQUIC ql);
static int  msquic_listen(HQUIC ql, const char *h, const char *p, nni_quic_listener *l);

/***************************** MsQuic Listener ******************************/

int
nni_quic_listener_init(void **argp)
{
	nni_quic_listener *l;

	if ((l = NNI_ALLOC_STRUCT(l)) == NULL) {
		return (NNG_ENOMEM);
	}

	nni_mtx_init(&l->mtx);

	l->closed  = false;
	l->started = false;

	l->ql      = NULL;

	// nni_aio_alloc(&l->qconaio, quic_listener_cb, (void *)l);
	nni_aio_list_init(&l->acceptq);
	nni_aio_list_init(&l->incomings);
	nni_atomic_init_bool(&l->fini);
	nni_atomic_init64(&l->ref);
	nni_atomic_inc64(&l->ref);

	// 0RTT is disabled by default
	l->enable_0rtt = false;
	// multi_stream is disabled by default
	l->enable_mltstrm = false;

	memset(&l->settings, 0, sizeof(QUIC_SETTINGS));

	*argp = l;
	return 0;
}

// All the instructments here should be done within guard of lock of listener.
// And also could be done repeatly.
static void
quic_listener_doclose(nni_quic_listener *l)
{
	nni_aio *aio;

	l->closed = true;

	while ((aio = nni_list_first(&l->acceptq)) != NULL) {
		nni_aio_list_remove(aio);
		nni_aio_finish_error(aio, NNG_ECLOSED);
	}
	while ((aio = nni_list_first(&l->incomings)) != NULL) {
		HQUIC qconn = nni_aio_get_prov_data(aio);
		nni_aio_list_remove(aio);
		nni_aio_free(aio);
		msquic_conn_fini(qconn);
	}
	if (l->ql != NULL) {
		msquic_listener_stop(l->ql);
	}
}

void
nni_quic_listener_close(nni_quic_listener *l)
{
	nni_mtx_lock(&l->mtx);
	quic_listener_doclose(l);
	nni_mtx_unlock(&l->mtx);
}


int
nni_quic_listener_listen(nni_quic_listener *l, const char *h, const char *p)
{
	int rv;

	nni_mtx_lock(&l->mtx);
	if (l->started) {
		nni_mtx_unlock(&l->mtx);
		return (NNG_ESTATE);
	}
	if (l->closed) {
		nni_mtx_unlock(&l->mtx);
		return (NNG_ECLOSED);
	}

	rv = msquic_listen(l->ql, h, p, l);
	if (rv != 0) {
		nni_mtx_unlock(&l->mtx);
		return rv;
	}

	l->started = true;
	nni_mtx_unlock(&l->mtx);

	return (0);
}

static void
quic_listener_cancel(nni_aio *aio, void *arg, int rv)
{
	nni_quic_listener *l = arg;

	// This is dead easy, because we'll ignore the completion if there
	// isn't anything to do the accept on!
	NNI_ASSERT(rv != 0);
	nni_mtx_lock(&l->mtx);
	if (nni_aio_list_active(aio)) {
		nni_aio_list_remove(aio);
		nni_aio_finish_error(aio, rv);
	}
	nni_mtx_unlock(&l->mtx);
}

static void
quic_listener_doaccept(nni_quic_listener *l)
{
	nni_aio *aio;

	while ((aio = nni_list_first(&l->acceptq)) != NULL) {
		int             rv;
		nni_aio *       aioc;
		nni_quic_conn * c;

		// Get the connection 
		if ((aioc = nni_list_first(&l->incomings)) == NULL) {
			// No wait and return immediately
			return;
		}
		c = nni_aio_get_prov_data(aioc); // Must exists
		nni_aio_list_remove(aioc);
		nni_aio_free(aioc);

		nni_aio_list_remove(aio);
		nni_aio_set_output(aio, 0, c);
		nni_aio_finish(aio, 0, 0);
	}
}


void
nni_quic_listener_accept(nni_quic_listener *l, nni_aio *aio)
{
	int rv;

	if (nni_aio_begin(aio)) {
		return;
	}
	nni_mtx_lock(&l->mtx);

	if (!l->started) {
		nni_mtx_unlock(&l->mtx);
		nni_aio_finish_error(aio, NNG_ESTATE);
		return;
	}

	if (l->closed) {
		nni_mtx_unlock(&l->mtx);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}

	if ((rv = nni_aio_schedule(aio, quic_listener_cancel, l)) != 0) {
		nni_mtx_unlock(&l->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_aio_list_append(&l->acceptq, aio);
	if (nni_list_first(&l->acceptq) == aio) {
		quic_listener_doaccept(l);
	}

	nni_mtx_unlock(&l->mtx);
}

void
nni_quic_listener_fini(nni_quic_listener *l)
{
	HQUIC ql;

	nni_mtx_lock(&l->mtx);
	quic_listener_doclose(l);
	ql = l->ql;
	nni_mtx_unlock(&l->mtx);

	if (ql != NULL) {
		msquic_listener_fini(ql);
	}
	nni_mtx_fini(&l->mtx);
	NNI_FREE_STRUCT(l);
}

/**************************** MsQuic Connection ****************************/

static void
quic_stream_cb(int events, void *arg)
{
	log_debug("[quic cb] start %d\n", events);
	nni_quic_conn     *c = arg;
	nni_quic_listener *l;
	nni_aio           *aio;

	if (!c)
		return;

	ss = c->session;
	l = c->listener;

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
		nni_aio_finish(aio, 0, nni_aio_count(aio));

		// Start next send only after finished the last send
		quic_stream_dowrite(c);

		nni_mtx_unlock(&c->mtx);
		break;
	case QUIC_STREAM_EVENT_START_COMPLETE:
		nni_mtx_lock(&l->mtx);

		// Push connection to incomings
		nni_aio_alloc(&aio, NULL, NULL);
		nni_aio_set_prov_data(aio, (void *)c);
		nni_aio_list_append(&l->incomings, aio);

		quic_listener_doaccept(l);

		nni_mtx_unlock(&ss->mtx);
		/*
		if (c->dial_aio) {
			// For upper layer to get the stream handle
			nni_aio_set_output(c->dial_aio, 0, c);

			nni_aio_list_remove(c->dial_aio);
			nni_aio_finish(c->dial_aio, 0, 0);
			c->dial_aio = NULL;
		}
		*/
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


int
nni_msquic_quic_listener_conn_alloc(nni_quic_conn **cp, nni_quic_session *ss)
{
	nni_quic_conn *c;
	if ((c = NNI_ALLOC_STRUCT(c)) == NULL) {
		return (NNG_ENOMEM);
	}

	c->closed   = false;
	c->dialer   = NULL;
	c->listener = ss->listener;
	c->session  = ss;

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

static int
quic_listener_session_alloc(nni_quic_session **ss, nni_quic_listener *l, HQUIC qconn)
{
	nni_quic_session *s;
	if ((s = NNI_ALLOC_STRUCT(s)) == NULL) {
		return (NNG_ENOMEM);
	}

	s->closed   = false;
	s->qconn    = qconn;
	s->listener = l;

	nni_aio_list_init(&s->conns);
	nni_mtx_init(&s->mtx);

	*ss = s;
	return (0);
}


/***************************** MsQuic Bindings *****************************/

static void
msquic_load_listener_config()
{
	return;
}

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

	log_debug("quic_strm_cb triggered! %d conn %p strm %p", Event->Type, c, stream);
	switch (Event->Type) {
	case QUIC_STREAM_EVENT_SEND_COMPLETE:
		log_debug("QUIC_STREAM_EVENT_SEND_COMPLETE!");
		if (Event->SEND_COMPLETE.Canceled) {
			log_warn("[strm][%p] Data sent Canceled: %d",
			    stream, Event->SEND_COMPLETE.Canceled);
		}
		// Priority msg send
		if ((aio = Event->SEND_COMPLETE.ClientContext) != NULL) {
			QUIC_BUFFER *buf = nni_aio_get_input(aio, 0);
			free(buf);
			Event->SEND_COMPLETE.ClientContext = NULL;
			// TODO free by user cb or msquic layer???
			// nni_msg *msg = nni_aio_get_msg(aio);
			// nni_msg_free(msg);
			nni_aio_finish(aio, 0, nni_aio_count(aio));
			break;
		}
		// Ordinary send
		quic_stream_cb(QUIC_STREAM_EVENT_SEND_COMPLETE, c);
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

		quic_stream_cb(QUIC_STREAM_EVENT_PEER_SEND_ABORTED, c);
		break;
	case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
		// The peer aborted its send direction of the stream.
		log_warn("[strm][%p] Peer send shut down\n", stream);
		MsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
		quic_stream_cb(QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN, c);
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
		quic_stream_cb(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE, c);
		break;
	case QUIC_STREAM_EVENT_START_COMPLETE:
		log_info(
		    "QUIC_STREAM_EVENT_START_COMPLETE [%p] ID: %ld Status: %d",
		    stream, Event->START_COMPLETE.ID,
		    Event->START_COMPLETE.Status);
		if (!Event->START_COMPLETE.PeerAccepted) {
			log_warn("Peer refused");
			quic_stream_cb(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE, c);
			break;
		}

		quic_stream_cb(QUIC_STREAM_EVENT_START_COMPLETE, c);

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

		quic_stream_cb(QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED, c);
		break;

	default:
		log_warn("Unknown Event Type %d", Event->Type);
		break;
	}
	return QUIC_STATUS_SUCCESS;
}


_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK) QUIC_STATUS QUIC_API
msquic_connection_cb(_In_ HQUIC Connection, _In_opt_ void *Context,
	_Inout_ QUIC_CONNECTION_EVENT *ev)
{
	nni_quic_session *ss    = Context;
	HQUIC             qconn = Connection;

	log_debug("msquic_connection_cb triggered! %d", ev->Type);
	switch (ev->Type) {
	case QUIC_CONNECTION_EVENT_CONNECTED:
		// The handshake has completed for the connection.
		// do not init any var here due to potential frequent reconnect
		log_info("[conn][%p] is Connected. Resumed Session %d", qconn,
		    ev->CONNECTED.SessionResumed);

		if (ss->listener->enable_0rtt) {
			MsQuic->ConnectionSendResumptionTicket(qconn, QUIC_SEND_RESUMPTION_FLAG_NONE, 0, NULL);
		}
		break;
	case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
		HQUIC qstrm = ev->PEER_STREAM_STARTED.Stream;
		QUIC_STREAM_OPEN_FLAGS flags = ev->PEER_STREAM_STARTED.Flags;

		int rv;
		nni_quic_conn *c;

		// Create a nni quic connection
		if ((rv = nni_msquic_quic_listener_conn_alloc(&c, ss)) != 0) {
			log_warn("Error in alloc new quic stream.");
			// msquic_conn_fini(qconn);
			nni_aio_list_remove(aio);
			nni_aio_finish_error(aio, rv);
			break;
		}

		log_info("[conn][%p] Peer stream %p started. flags %d.", qconn, qstrm, flags);
		MsQuic->SetCallbackHandler(qstrm, (void *)msquic_strm_cb, c);

		break;
	case QUIC_CONNECTION_EVENT_RESUMED:
		// TODO
		log_warn("[conn][%p] This connection is resumed.", qconn);
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		log_warn("[conn][%p] Shutdown by transport, 0x%x, Error Code %llu\n",
		    qconn, ev->SHUTDOWN_INITIATED_BY_TRANSPORT.Status,
		    (unsigned long long)
		        ev->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode);
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		// The connection was explicitly shut down by the peer.
		log_warn("[conn][%p] "
		         "QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER, "
		         "0x%llu\n",
		    qconn,
		    (unsigned long long)ev->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		log_info("[conn][%p] QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: All done\n\n", qconn);
		break;
	case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
		log_warn("[conn][%p] Resumption ticket received (%u bytes):\n",
		    Connection, ev->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
		break;
	case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
		log_info("QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED");
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
		log_warn("Unknown event type %d!", ev->Type);
		break;
	}
	return QUIC_STATUS_SUCCESS;
}


_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK) QUIC_STATUS QUIC_API
msquic_listener_cb(_In_ HQUIC ql, _In_opt_ void *arg, _Inout_ QUIC_LISTENER_EVENT *ev)
{
	int rv;
	HQUIC qconn;
	const QUIC_NEW_CONNECTION_INFO *qinfo;
	QUIC_STATUS rv = QUIC_STATUS_NOT_SUPPORTED;
	nni_quic_listener *l = arg;
	nni_aio *aio;
	nni_quic_session *ss;

	switch (ev->Type) {
	case QUIC_LISTENER_EVENT_NEW_CONNECTION:
		qconn = ev->NEW_CONNECTION.Connection;
		qinfo = ev->NEW_CONNECTION.Info;

		rv = quic_listener_session_alloc(&ss, l, qconn);
		if (rv != 0) {
			log_error("error in alloc session");
			break;
		}

		MsQuic->SetCallbackHandler(qconn, msquic_connection_cb, ss);
		rv = MsQuic->ConnectionSetConfiguration(qconn, configuration);
		break;
	case QUIC_LISTENER_EVENT_STOP_COMPLETE:
		break;
	default:
		break;
	}

	return rv;
}

static int
msquic_listen(HQUIC ql, const char *h, const char *p, nni_quic_listener *l)
{
	QUIC_ADDR addr;
	QUIC_STATUS rv = 0;

	QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
	QuicAddrSetPort(&addr, atoi(p));

	msquic_load_listener_config();

	if (QUIC_FAILED(rv = MsQuic->ListenerOpen(registration, msquic_listener_cb, (void *)l, &ql))) {
		log_error("error in listen open %ld", rv);
		goto error;
	}

	if (QUIC_FAILED(rv = MsQuic->ListenerStart(ql, &quic_alpn, 1, &addr))) {
		log_error("error in listen start %ld", rv);
		goto error;
	}

	return rv;

error:
	if (ql != NULL) {
		msquic_listener_fini(ql);
	}
	return rv;
}

static void
msquic_listener_stop(HQUIC ql)
{
	MsQuic->ListenerStop(ql);
}

static void
msquic_listener_fini(HQUIC ql)
{
	MsQuic->ListenerClose(ql);
}

