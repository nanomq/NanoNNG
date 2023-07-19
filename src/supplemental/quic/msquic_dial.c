//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// #if defined(NNG_ENABLE_QUIC) // && defined(NNG_QUIC_MSQUIC)

#include "core/defs.h"
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
};

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

void
nni_quic_dialer_fini(void *arg)
{
	nni_quic_dialer *d = arg;
	NNI_ARG_UNUSED(d);
}

static void
quic_dialer_strm_cancel(nni_aio *aio, void *arg, int rv)
{
	nni_tcp_dialer *d = arg;
	nni_tcp_conn *  c;

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
	nni_tcp_dialer *d = arg;
	nni_tcp_conn *  c;

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
nni_quic_dial(void *arg, const char *url, nni_aio *aio)
{
	nni_quic_dialer *d = arg;
	nni_quic_conn *  c;
	bool             ismain = false;

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
	// TODO check if there are any quic streams to the url.
	ismain = true;

	if ((rv = nni_aio_schedule(aio, quic_dialer_cancel, d)) != 0) {
		goto error;
	}

	// pass c to the dialer for getting it in msquic_strm_connect
	d->currcon = c;

	if (ismain) {
		// Make a quic connection to the url.
		// Create stream after connection is established.
		msquic_connect(url, d);
	} else {
		msquic_strm_connect(d->qconn, d);
	}

	nni_mtx_unlock(&d->mtx);

	return;
error:

}

// Close the pending connection.
void
nni_quic_dialer_close(void *arg)
{
	nni_quic_dialer *d = arg;
	NNI_ARG_UNUSED(d);
}

/**************************** MsQuic Connection ****************************/

struct nni_quic_conn {
	nng_stream      stream;
	nni_posix_pfd * pfd;
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
};

static void
quic_cb(nni_posix_pfd *pfd, unsigned events, void *arg)
{
}

static void
quic_free(void *arg)
{
	nni_quic_conn *c = arg;
	// nni_reap(&tcp_reap_list, c);
}

static void
quic_close(void *arg)
{
	nni_quic_conn *c = arg;
}

static void
quic_recv(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
}

static void
quic_send(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
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
	c->stream.s_close = quic_close;
	c->stream.s_recv  = quic_recv;
	c->stream.s_send  = quic_send;
	c->stream.s_get   = quic_get;
	c->stream.s_set   = quic_set;

	*cp = c;
	return (0);
}

void
nni_msquic_quic_init(nni_quic_conn *c, nni_posix_pfd *pfd)
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

		nng_aio_finish(d->qconaio);

		// only create main stream/pipe it there is none.
		if (qsock->pipe == NULL) {
			// First time to establish QUIC pipe
			if ((qsock->pipe = nng_alloc(pipe_ops->pipe_size)) == NULL) {
				log_error("Failed in allocating pipe.");
				break;
			}
			mqtt_sock = nni_sock_proto_data(qsock->sock);
			pipe_ops->pipe_init(
			    qsock->pipe, (nni_pipe *) qsock, mqtt_sock);
		}
		pipe_ops->pipe_start(qsock->pipe);
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		// The connection has been shut down by the transport.
		// Generally, this is the expected way for the connection to
		// shut down with this protocol, since we let idle timeout kill
		// the connection.
		if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status ==
		    QUIC_STATUS_CONNECTION_IDLE) {
			log_warn(
			    "[conn][%p] Successfully shut down connection on idle.\n",
			    qconn);
		} else if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status ==
		    QUIC_STATUS_CONNECTION_TIMEOUT) {
			log_warn("[conn][%p] Successfully shut down on "
			         "CONNECTION_TIMEOUT.\n",
			    qconn);
		}
		log_warn("[conn][%p] Shut down by transport, 0x%x, Error Code "
		         "%llu\n",
		    qconn, Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status,
		    (unsigned long long)
		        Event->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode);
		if (qsock->reason_code == 0)
            	qsock->reason_code = SERVER_UNAVAILABLE;
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		// The connection was explicitly shut down by the peer.
		log_warn("[conn][%p] "
		         "QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER, "
		         "0x%llu\n",
		    qconn,
		    (unsigned long long)
		        Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
		if (qsock->reason_code == 0)
			qsock->reason_code = SERVER_SHUTTING_DOWN;
		break;
	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		// The connection has completed the shutdown process and is
		// ready to be safely cleaned up.
		log_info("[conn][%p] QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: All done\n\n", qconn);
		nni_mtx_lock(&qsock->mtx);
		if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
			// explicitly shutdon on protocol layer.
			MsQuic->ConnectionClose(qconn);
		}

		// Close and finite nng pipe ONCE disconnect
		if (qsock->pipe) {
			log_warn("Quic reconnect failed or disconnected!");
			pipe_ops->pipe_stop(qsock->pipe);
			pipe_ops->pipe_close(qsock->pipe);
			pipe_ops->pipe_fini(qsock->pipe);
			qsock->pipe = NULL;
			// No bridge_node if NOT bridge mode
			if (bridge_node && (bridge_node->hybrid || qsock->closed)) {
				nni_mtx_unlock(&qsock->mtx);
				break;
			}
		}

		nni_mtx_unlock(&qsock->mtx);
		if (qsock->closed == true) {
			nni_sock_rele(qsock->sock);
		} else {
			nni_aio_finish(&qsock->close_aio, 0, 0);
		}
		/*
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
		if (qsock->enable_0rtt == false) {
			log_warn("[conn][%p] Ignore ticket due to turn off the 0RTT");
			break;
		}
		qsock->rticket_sz = Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength;
		memcpy(qsock->rticket, Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicket,
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
				Event->PEER_CERTIFICATE_RECEIVED.Chain, qsock->cacert))) {
			log_error("[conn][%p] Invalid certificate file received from the peer");
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

const QUIC_API_TABLE *MsQuic = NULL;

// Config for msquic
const QUIC_REGISTRATION_CONFIG quic_reg_config = {
	"mqtt",
	QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

const QUIC_BUFFER quic_alpn = {
	sizeof("mqtt") - 1,
	(uint8_t *) "mqtt"
};

HQUIC registration;
HQUIC configuration;

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
	}
}

static int is_msquic_inited = 0;

static void
msquic_open()
{
	if (is_msquic_inited == 1)
		return;

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
	return;

error:
	msquic_close();
}

static int
msquic_connect(const char *url, nni_quic_dialer *d)
{
	QUIC_STATUS  rv;
	HQUIC        conn = NULL;
	nng_url     *url_s;

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

	nng_url_parse(&url_s, url);
	// TODO maybe something wrong happened
	for (size_t i = 0; i < strlen(url_s->u_host); ++i)
		if (url_s->u_host[i] == ':') {
			url_s->u_host[i] = '\0';
			break;
		}

	log_info("Quic connecting... %s:%s", url_s->u_host, url_s->u_port);

	if (QUIC_FAILED(rv = MsQuic->ConnectionStart(conn, configuration,
	        QUIC_ADDRESS_FAMILY_UNSPEC, url_s->u_host, atoi(url_s->u_port)))) {
		log_error("Failed in ConnectionStart, 0x%x!", rv);
		goto error;
	}

	return 0;
}

static int
msquic_strm_connect(HQUIC qconn, nni_quic_dialer *d)
{
	HQUIC          strm = NULL;
	QUIC_STATUS    rv;
	nni_quic_conn *c;

	nni_mtx_lock(&d->mtx);

	c = d->currcon;
	d->currcon = NULL;

	rv = MsQuic->StreamOpen(qs->qconn, QUIC_STREAM_OPEN_FLAG_NONE,
	        msquic_strm_cb, (void *)c, &strm);
	if (QUIC_FAILED(rv)) {
		log_error("StreamOpen failed, 0x%x!\n", rv);
		goto error;
	}

	log_debug("[strm][%p] Starting...", strm);

	if (QUIC_FAILED(rv = MsQuic->StreamStart(strm, QUIC_STREAM_START_FLAG_NONE))) {
		log_error("quic stream start failed, 0x%x!\n", rv);
		MsQuic->StreamClose(strm);
		goto error;
	}

	// Not ready for receiving
	MsQuic->StreamReceiveSetEnabled(qstrm->stream, FALSE);
	c->qstrm = strm;

	log_debug("[strm][%p] Done...\n", strm);

	nni_mtx_unlock(&d->mtx);

	return;
error:

	nng_free(qstrm, sizeof(quic_strm_t));

	return (-2);
}
