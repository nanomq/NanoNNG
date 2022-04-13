//
// Copyright 2021 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
#include <conf.h>
#include <mqtt_db.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/nng_impl.h"
#include "core/sockimpl.h"
#include "nng/nng_debug.h"

#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "supplemental/mqtt/mqtt_qos_db_api.h"

// TCP transport.   Platform specific TCP operations must be
// supplied as well.

typedef struct tcptran_pipe tcptran_pipe;
typedef struct tcptran_ep   tcptran_ep;

// tcp_pipe is one end of a TCP connection.
struct tcptran_pipe {
	nng_stream *conn;
	nni_pipe   *npipe; // for statitical
	// uint16_t        peer;		//reserved for MQTT sdk version
	// uint16_t        proto;
	size_t          rcvmax;
	size_t          gotrxhead;
	size_t          wantrxhead;
	size_t          qlength; // length of qos_buf
	bool            closed;
	bool            busy; // indicator for qos ack & aio
	uint8_t         txlen[NANO_MIN_PACKET_LEN];
	uint8_t         rxlen[NNI_NANO_MAX_HEADER_SIZE];
	uint8_t        *conn_buf;
	uint8_t        *qos_buf; // msg trunk for qos & V4/V5 conversion
	nni_aio        *txaio;
	nni_aio        *rxaio;
	nni_aio        *qsaio;
	nni_aio        *rpaio;
	nni_aio        *negoaio;
	nni_lmq         rslmq;
	nni_msg        *rxmsg, *cnmsg;
	nni_mtx         mtx;
	conn_param     *tcp_cparam;
	nni_list        recvq;
	nni_list        sendq;
	nni_list_node   node;
	tcptran_ep     *ep;
	nni_atomic_flag reaped;
	nni_reap_node   reap;
	// MQTT V5
	subinfo *subinfo;
	uint16_t qrecv_quota;
	uint32_t qsend_quota;
};

struct tcptran_ep {
	nni_mtx mtx;
	// uint16_t             proto;
	size_t               rcvmax;
	bool                 fini;
	bool                 started;
	bool                 closed;
	nng_url             *url;
	nng_sockaddr         src;
	int                  refcnt; // active pipes
	nni_aio             *useraio;
	nni_aio             *connaio;
	nni_aio             *timeaio;
	nni_list             busypipes; // busy pipes -- ones passed to socket
	nni_list             waitpipes; // pipes waiting to match to socket
	nni_list             negopipes; // pipes busy negotiating
	nni_reap_node        reap;
	nng_stream_listener *listener;
#ifdef NNG_ENABLE_STATS
	nni_stat_item st_rcv_max;
#endif
};

static void tcptran_pipe_send_start(tcptran_pipe *);
static void tcptran_pipe_recv_start(tcptran_pipe *);
static void nmq_tcptran_pipe_send_cb(void *);
static void nmq_tcptran_pipe_qos_send_cb(void *);
static void tcptran_pipe_recv_cb(void *);
static void tcptran_pipe_nego_cb(void *);
static void tcptran_ep_fini(void *);
static void tcptran_pipe_fini(void *);

static inline void
nmq_pipe_send_start_v5(tcptran_pipe *p, nni_msg *msg, nni_aio *aio);

static nni_reap_list tcptran_ep_reap_list = {
	.rl_offset = offsetof(tcptran_ep, reap),
	.rl_func   = tcptran_ep_fini,
};

static nni_reap_list tcptran_pipe_reap_list = {
	.rl_offset = offsetof(tcptran_pipe, reap),
	.rl_func   = tcptran_pipe_fini,
};

static void
tcptran_init(void)
{
}

static void
tcptran_fini(void)
{
}

static void
tcptran_pipe_close(void *arg)
{
	tcptran_pipe *p = arg;
	// nni_pipe *    npipe = p->npipe;

	nni_mtx_lock(&p->mtx);
	p->closed = true;
	nni_lmq_flush(&p->rslmq);
	nni_mtx_unlock(&p->mtx);

	nni_aio_close(p->rxaio);
	nni_aio_close(p->rpaio);
	nni_aio_close(p->txaio);
	nni_aio_close(p->qsaio);
	nni_aio_close(p->negoaio);

	nng_stream_close(p->conn);
	debug_syslog("tcptran_pipe_close\n");
}

static void
tcptran_pipe_stop(void *arg)
{
	tcptran_pipe *p = arg;

	nni_aio_stop(p->qsaio);
	nni_aio_stop(p->rpaio);
	nni_aio_stop(p->rxaio);
	nni_aio_stop(p->txaio);
	nni_aio_stop(p->negoaio);
}

static int
tcptran_pipe_init(void *arg, nni_pipe *npipe)
{
	debug_msg("************tcptran_pipe_init************");
	tcptran_pipe *p = arg;

	nni_pipe_set_conn_param(npipe, p->tcp_cparam);
	p->npipe = npipe;
#ifndef NNG_SUPP_SQLITE
	nni_qos_db_init_id_hash(npipe->nano_qos_db);
#endif
	p->conn_buf = NULL;
	p->busy     = false;

	nni_lmq_init(&p->rslmq, 16);
	p->qos_buf = nng_zalloc(16 + NNI_NANO_MAX_PACKET_SIZE);
	// the size limit of qos_buf reserve 1 byte for property length
	p->qlength = 16 + NNI_NANO_MAX_PACKET_SIZE;
	return (0);
}

static void
tcptran_pipe_fini(void *arg)
{
	tcptran_pipe *p = arg;
	tcptran_ep   *ep;

	tcptran_pipe_stop(p);
	if ((ep = p->ep) != NULL) {
		nni_mtx_lock(&ep->mtx);
		nni_list_node_remove(&p->node);
		ep->refcnt--;
		if (ep->fini && (ep->refcnt == 0)) {
			nni_reap(&tcptran_ep_reap_list, ep);
		}
		nni_mtx_unlock(&ep->mtx);
	}

	nng_free(p->qos_buf, 16 + NNI_NANO_MAX_PACKET_SIZE);
	nni_aio_free(p->qsaio);
	nni_aio_free(p->rpaio);
	nni_aio_free(p->rxaio);
	nni_aio_free(p->txaio);
	nni_aio_free(p->negoaio);
	nng_stream_free(p->conn);
	nni_msg_free(p->rxmsg);
	nni_lmq_fini(&p->rslmq);
	nni_mtx_fini(&p->mtx);
	NNI_FREE_STRUCT(p);
}

static void
tcptran_pipe_reap(tcptran_pipe *p)
{
	if (!nni_atomic_flag_test_and_set(&p->reaped)) {
		if (p->conn != NULL) {
			nng_stream_close(p->conn);
		}
		nni_reap(&tcptran_pipe_reap_list, p);
	}
}

static int
tcptran_pipe_alloc(tcptran_pipe **pipep)
{
	tcptran_pipe *p;
	int           rv;

	if ((p = NNI_ALLOC_STRUCT(p)) == NULL) {
		return (NNG_ENOMEM);
	}
	nni_mtx_init(&p->mtx);
	if (((rv = nni_aio_alloc(&p->txaio, nmq_tcptran_pipe_send_cb, p)) !=
	        0) ||
	    ((rv = nni_aio_alloc(
	          &p->qsaio, nmq_tcptran_pipe_qos_send_cb, p)) != 0) ||
	    ((rv = nni_aio_alloc(&p->rpaio, NULL, p)) != 0) ||
	    ((rv = nni_aio_alloc(&p->rxaio, tcptran_pipe_recv_cb, p)) != 0) ||
	    ((rv = nni_aio_alloc(&p->negoaio, tcptran_pipe_nego_cb, p)) !=
	        0)) {
		tcptran_pipe_fini(p);
		return (rv);
	}
	nni_aio_list_init(&p->recvq);
	nni_aio_list_init(&p->sendq);
	nni_atomic_flag_reset(&p->reaped);

	*pipep = p;

	return (0);
}

static void
tcptran_ep_match(tcptran_ep *ep)
{
	nni_aio      *aio;
	tcptran_pipe *p;

	if (((aio = ep->useraio) == NULL) ||
	    ((p = nni_list_first(&ep->waitpipes)) == NULL)) {
		return;
	}
	nni_list_remove(&ep->waitpipes, p);
	nni_list_append(&ep->busypipes, p);
	ep->useraio = NULL;
	p->rcvmax   = ep->rcvmax;
	nni_aio_set_output(aio, 0, p);
	nni_aio_finish(aio, 0, 0);
}

/**
 * MQTT protocal negotiate
 * deal with CONNECT packet
 * Fixed header to variable header
 * receive multiple times for complete data packet then reply ACK in protocol
 * layer iov_len limits the length readv reads
 * TODO independent with nng SP
 */
static void
tcptran_pipe_nego_cb(void *arg)
{
	tcptran_pipe *p   = arg;
	tcptran_ep   *ep  = p->ep;
	nni_aio      *aio = p->negoaio;
	nni_aio      *uaio;
	uint32_t      len;
	int           rv, len_of_varint = 0;

	debug_msg("start tcptran_pipe_nego_cb max len %ld pipe_addr %p\n",
	    NANO_CONNECT_PACKET_LEN, p);
	nni_mtx_lock(&ep->mtx);

	if ((rv = nni_aio_result(aio)) != 0) {
		goto error;
	}

	// calculate number of bytes received
	if (p->gotrxhead < p->wantrxhead) {
		p->gotrxhead += nni_aio_count(aio);
	}

	// recv fixed header
	if (p->gotrxhead < NNI_NANO_MAX_HEADER_SIZE) {
		nni_iov iov;
		iov.iov_len = NNI_NANO_MAX_HEADER_SIZE - p->gotrxhead;
		iov.iov_buf = &p->rxlen[p->gotrxhead];
		nni_aio_set_iov(aio, 1, &iov);
		nng_stream_recv(p->conn, aio);
		nni_mtx_unlock(&ep->mtx);
		return;
	}
	if (p->gotrxhead == NNI_NANO_MAX_HEADER_SIZE) {
		if (p->rxlen[0] != CMD_CONNECT) {
			debug_msg("CMD TYPE %x", p->rxlen[0]);
			rv = NNG_EPROTO;
			goto error;
		}
		len =
		    get_var_integer(p->rxlen + 1, (uint32_t *) &len_of_varint);
		p->wantrxhead = len + 1 + len_of_varint;
		rv            = (p->wantrxhead >= NANO_CONNECT_PACKET_LEN) ? 0
		                                                           : NNG_EPROTO;
		if (rv != 0) {
			goto error;
		}
	}

	// we have finished the fixed header
	if (p->gotrxhead < p->wantrxhead) {
		nni_iov iov;
		iov.iov_len = p->wantrxhead - p->gotrxhead;
		if (p->conn_buf == NULL) {
			p->conn_buf = nng_alloc(p->wantrxhead);
			memcpy(p->conn_buf, p->rxlen, p->gotrxhead);
		}
		iov.iov_buf = &p->conn_buf[p->gotrxhead];
		nni_aio_set_iov(aio, 1, &iov);
		nng_stream_recv(p->conn, aio);
		nni_mtx_unlock(&ep->mtx);
		return;
	}

	// We have both sent and received the CONNECT headers.
	// CONNECT packet serialization

	if (p->gotrxhead >= p->wantrxhead) {
		if (p->tcp_cparam == NULL) {
			conn_param_alloc(&p->tcp_cparam);
		}
		if (conn_handler(p->conn_buf, p->tcp_cparam) == 0) {
			nng_free(p->conn_buf, p->wantrxhead);
			p->conn_buf = NULL;
			// Connection is accepted.
			if (p->tcp_cparam->pro_ver == 5) {
				p->qsend_quota = p->tcp_cparam->rx_max;
			}
			nni_list_remove(&ep->negopipes, p);
			nni_list_append(&ep->waitpipes, p);
			tcptran_ep_match(ep);
			nni_mtx_unlock(&ep->mtx);
			return;
		} else {
			rv = NNG_EPROTO;
			nng_free(p->conn_buf, p->wantrxhead);
			conn_param_free(p->tcp_cparam);
			goto error;
		}
	}

	nni_mtx_unlock(&ep->mtx);
	debug_msg("^^^^^^^^^^end of tcptran_pipe_nego_cb^^^^^^^^^^\n");
	return;

error:
	// If the connection is closed, we need to pass back a different
	// error code.  This is necessary to avoid a problem where the
	// closed status is confused with the accept file descriptor
	// being closed.
	if (rv == NNG_ECLOSED) {
		rv = NNG_ECONNSHUT;
	}
	nng_stream_close(p->conn);

	if ((uaio = ep->useraio) != NULL) {
		ep->useraio = NULL;
		nni_aio_finish_error(uaio, rv);
	}
	nni_mtx_unlock(&ep->mtx);
	tcptran_pipe_reap(p);
	debug_msg("connect nego error rv: %d!", rv);
	return;
}

static void
nmq_tcptran_pipe_qos_send_cb(void *arg)
{
	tcptran_pipe *p = arg;
	nni_msg      *msg;
	nni_aio      *qsaio = p->qsaio;
	uint8_t       type;

	if (nni_aio_result(qsaio) != 0) {
		nni_msg_free(nni_aio_get_msg(qsaio));
		nni_aio_set_msg(qsaio, NULL);
		tcptran_pipe_close(p);
		return;
	}
	nni_mtx_lock(&p->mtx);

	msg  = nni_aio_get_msg(p->qsaio);
	type = nni_msg_cmd_type(msg);

	if (p->tcp_cparam->pro_ver == 5) {
		(type == CMD_PUBCOMP || type == PUBACK) ? p->qrecv_quota++
		                                        : p->qrecv_quota;
	}
	nni_msg_free(msg);
	if (nni_lmq_get(&p->rslmq, &msg) == 0) {
		nni_iov iov[2];
		iov[0].iov_len = nni_msg_header_len(msg);
		iov[0].iov_buf = nni_msg_header(msg);
		iov[1].iov_len = nni_msg_len(msg);
		iov[1].iov_buf = nni_msg_body(msg);
		nni_aio_set_msg(p->qsaio, msg);
		// send it down...
		nni_aio_set_iov(p->qsaio, 2, iov);
		nng_stream_send(p->conn, p->qsaio);
		p->busy = true;
		nni_mtx_unlock(&p->mtx);
		return;
	}
	p->busy = false;
	nni_aio_set_msg(qsaio, NULL);
	nni_mtx_unlock(&p->mtx);
	return;
}

static void
nmq_tcptran_pipe_send_cb(void *arg)
{
	tcptran_pipe *p = arg;
	int           rv;
	nni_aio      *aio;
	uint8_t      *header;
	uint8_t       flag, cmd;
	size_t        n;
	nni_msg      *msg;
	nni_aio      *txaio = p->txaio;

	nni_mtx_lock(&p->mtx);
	aio = nni_list_first(&p->sendq);

	debug_msg("############### nmq_tcptran_pipe_send_cb ################");

	if ((rv = nni_aio_result(txaio)) != 0) {
		nni_pipe_bump_error(p->npipe, rv);
		nni_aio_list_remove(aio);
		nni_mtx_unlock(&p->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}

	n = nni_aio_count(txaio);
	nni_aio_iov_advance(txaio, n);
	debug_msg(
	    "tcp socket sent %ld bytes iov %ld", n, nni_aio_iov_count(txaio));

	// more bytes to send
	if (nni_aio_iov_count(txaio) > 0) {
		nng_stream_send(p->conn, txaio);
		nni_mtx_unlock(&p->mtx);
		return;
	}

	msg = nni_aio_get_msg(aio);
	if (nni_aio_get_prov_data(txaio) != NULL) {
		// msgs left behind due to multiple topics matched
		nmq_pipe_send_start_v5(p, msg, aio);
		nni_mtx_unlock(&p->mtx);
		return;
	}
	nni_aio_list_remove(aio);
	tcptran_pipe_send_start(p);

	if (msg == NULL) {
		nni_mtx_unlock(&p->mtx);
		// msg is lost due to flow control
		nni_aio_finish(aio, 0, 0);
		return;
	}

	n   = nni_msg_len(msg);
	cmd = nni_msg_cmd_type(msg);
	if (cmd == CMD_CONNACK) {
		header = nni_msg_header(msg);
		// parse result code TODO verify bug
		flag = header[3];
	}
	// nni_pipe_bump_tx(p->npipe, n);
	// free qos buffer
	if (p->qlength > 16 + NNI_NANO_MAX_PACKET_SIZE) {
		nng_free(p->qos_buf, p->qlength);
		p->qos_buf = nng_alloc(16 + NNI_NANO_MAX_PACKET_SIZE);
	}
	nni_mtx_unlock(&p->mtx);

	nni_aio_set_msg(aio, NULL);
	nni_msg_free(msg);
	if (cmd == CMD_CONNACK && flag != 0x00) {
		nni_aio_finish_error(aio, NNG_ECLOSED);
	} else {
		nni_aio_finish_sync(aio, 0, n);
	}
}

/*
 * deal with MQTT protocol
 * insure read complete MQTT packet from socket
 */
static void
tcptran_pipe_recv_cb(void *arg)
{
	nni_aio      *aio;
	nni_iov       iov[2];
	uint8_t       type;
	uint32_t      len = 0, rv, pos = 1;
	size_t        n;
	nni_msg      *msg, *qmsg;
	tcptran_pipe *p     = arg;
	nni_aio      *rxaio = p->rxaio;
	conn_param   *cparam;
	bool          ack   = false;
	nni_pipe     *npipe = p->npipe;

	debug_msg("tcptran_pipe_recv_cb %p\n", p);
	nni_mtx_lock(&p->mtx);

	aio = nni_list_first(&p->recvq);

	if ((rv = nni_aio_result(rxaio)) != 0) {
		debug_msg("nni aio error!! %d\n", rv);
		rv = NMQ_SERVER_BUSY;
		goto recv_error;
	}

	n = nni_aio_count(rxaio);
	p->gotrxhead += n;

	nni_aio_iov_advance(rxaio, n);
	// not receive enough bytes, deal with remaining length
	len = get_var_integer(p->rxlen, &pos);
	debug_msg("new %ld recevied %ld header %x %d pos: %d len : %d", n,
	    p->gotrxhead, p->rxlen[0], p->rxlen[1], pos, len);
	debug_msg("still need byte count:%ld > 0\n", nni_aio_iov_count(rxaio));

	if (nni_aio_iov_count(rxaio) > 0) {
		debug_msg("got: %x %x, %ld!!\n", p->rxlen[0], p->rxlen[1],
		    strlen((char *) p->rxlen));
		nng_stream_recv(p->conn, rxaio);
		nni_mtx_unlock(&p->mtx);
		return;
	} else if (p->gotrxhead <= NNI_NANO_MAX_HEADER_SIZE &&
	    p->rxlen[p->gotrxhead - 1] > 0x7f) {
		// length error
		if (p->gotrxhead == NNI_NANO_MAX_HEADER_SIZE) {
			rv = NNG_EMSGSIZE;
			goto recv_error;
		}
		// same packet, continue receving next byte of remaining length
		iov[0].iov_buf = &p->rxlen[p->gotrxhead];
		iov[0].iov_len = 1;
		nni_aio_set_iov(rxaio, 1, iov);
		nng_stream_recv(p->conn, rxaio);
		nni_mtx_unlock(&p->mtx);
		return;
	} else if (len == 0 && n == 2) {
		if ((p->rxlen[0] & 0XFF) == CMD_PINGREQ) {
			// TODO set timeout in case it never finish
			nng_aio_wait(p->rpaio);
			p->txlen[0]    = CMD_PINGRESP;
			p->txlen[1]    = 0x00;
			iov[0].iov_len = 2;
			iov[0].iov_buf = &p->txlen;
			// send CMD_PINGRESP down...
			nni_aio_set_iov(p->rpaio, 1, iov);
			nng_stream_send(p->conn, p->rpaio);
			goto notify;
		}
	}

	// finish fixed header
	p->wantrxhead = len + p->gotrxhead;
	cparam        = p->tcp_cparam;

	if (p->rxmsg == NULL) {
		// We should have gotten a message header. len -> remaining
		// length to define how many bytes left
		debug_msg("pipe %p header got: %x %x %x %x %x, %ld!!\n", p,
		    p->rxlen[0], p->rxlen[1], p->rxlen[2], p->rxlen[3],
		    p->rxlen[4], p->wantrxhead);
		// Make sure the message payload is not too big.  If it is
		// the caller will shut down the pipe.
		if (len > NANO_MAX_RECV_PACKET_SIZE) {
			debug_msg("size error 0x95\n");
			rv = NMQ_PACKET_TOO_LARGE;
			goto recv_error;
		}

		if ((rv = nni_msg_alloc(&p->rxmsg, (size_t) len)) != 0) {
			debug_syslog("mem error %ld\n", (size_t) len);
			rv = NMQ_SERVER_UNAVAILABLE;
			goto recv_error;
		}

		// Submit the rest of the data for a read -- seperate Fixed
		// header with variable header and so on
		//  we want to read the entire message now.
		if (len != 0) {
			iov[0].iov_buf = nni_msg_body(p->rxmsg);
			iov[0].iov_len = (size_t) len;

			nni_aio_set_iov(rxaio, 1, iov);
			// second recv action
			nng_stream_recv(p->conn, rxaio);
			nni_mtx_unlock(&p->mtx);
			return;
		}
	}

	// We read a message completely.  Let the user know the good news. use
	// as application message callback of users
	nni_aio_list_remove(aio);
	msg      = p->rxmsg;
	p->rxmsg = NULL;
	n        = nni_msg_len(msg);
	type     = p->rxlen[0] & 0xf0;

	fixed_header_adaptor(p->rxlen, msg);
	nni_msg_set_conn_param(msg, cparam);
	// duplicated with fixed_header_adaptor
	nni_msg_set_remaining_len(msg, len);
	nni_msg_set_cmd_type(msg, type);
	debug_msg("remain_len %d cparam %p clientid %s username %s proto %d\n",
	    len, cparam, cparam->clientid.body, cparam->username.body,
	    cparam->pro_ver);

	// set the payload pointer of msg according to packet_type
	debug_msg("The type of msg is %x", type);
	uint16_t  packet_id   = 0;
	uint8_t   reason_code = 0;
	property *prop        = NULL;
	uint8_t   ack_cmd     = 0;
	if (type == CMD_PUBLISH) {
		nni_msg_set_timestamp(msg, nng_clock());
		uint8_t qos_pac = nni_msg_get_pub_qos(msg);
		if (qos_pac > 0) {
			// flow control, check rx_max
			// recv_quota as length of lmq
			if (p->tcp_cparam->pro_ver == 5) {
				if (p->qrecv_quota > 0) {
					p->qrecv_quota--;
				} else {
					rv = NMQ_RECEIVE_MAXIMUM_EXCEEDED;
					goto recv_error;
				}
			}
			if (qos_pac == 1) {
				ack_cmd = CMD_PUBACK;
			} else if (qos_pac == 2) {
				ack_cmd = CMD_PUBREC;
			}
			packet_id = nni_msg_get_pub_pid(msg);
			ack       = true;
		}
	} else if (type == CMD_PUBREC) {
		if (nmq_pubres_decode(msg, &packet_id, &reason_code, &prop,
		        cparam->pro_ver) != 0) {
			debug_msg("decode PUBREC variable header failed!");
		}
		ack_cmd = CMD_PUBREL;
		ack     = true;
	} else if (type == CMD_PUBREL) {
		if (nmq_pubres_decode(msg, &packet_id, &reason_code, &prop,
		        cparam->pro_ver) != 0) {
			debug_msg("decode PUBREL variable header failed!");
		}
		ack_cmd = CMD_PUBCOMP;
		ack     = true;
	} else if (type == CMD_PUBACK || type == CMD_PUBCOMP) {
		if (nmq_pubres_decode(msg, &packet_id, &reason_code, &prop,
		        cparam->pro_ver) != 0) {
			debug_msg("decode PUBACK or PUBCOMP variable header "
			          "failed!");
		}
		// MQTT V5 flow control
		if (p->tcp_cparam->pro_ver == 5) {
			property_free(prop);
			p->qsend_quota++;
		}
	}
	if (ack == true) {
		// alloc a msg here costs memory. However we must do it for the
		// sake of compatibility with nng.
		if ((rv = nni_msg_alloc(&qmsg, 0)) != 0) {
			ack = false;
			rv  = NMQ_SERVER_BUSY;
			goto recv_error;
		}
		// TODO set reason code or property here if necessary

		nni_msg_set_cmd_type(qmsg, ack_cmd);
		nmq_msgack_encode(
		    qmsg, packet_id, reason_code, prop, cparam->pro_ver);
		nmq_pubres_header_encode(qmsg, ack_cmd);
		// if (prop != NULL) {
		// nni_msg_proto_set_property(qmsg, prop);
		// }
		// aio_begin?
		if (p->busy == false) {
			iov[0].iov_len = nni_msg_header_len(qmsg);
			iov[0].iov_buf = nni_msg_header(qmsg);
			iov[1].iov_len = nni_msg_len(qmsg);
			iov[1].iov_buf = nni_msg_body(qmsg);
			p->busy        = true;
			nni_aio_set_msg(p->qsaio, qmsg);
			// send ACK down...
			nni_aio_set_iov(p->qsaio, 2, iov);
			nng_stream_send(p->conn, p->qsaio);
		} else {
			if (nni_lmq_full(&p->rslmq)) {
				// Make space for the new message. TODO add max
				// limit of msgq len in conf
				if (nni_lmq_cap(&p->rslmq) <=
				    NANO_MAX_QOS_PACKET) {
					if ((rv = nni_lmq_resize(&p->rslmq,
					         nni_lmq_cap(&p->rslmq) *
					             2)) == 0) {
						nni_lmq_put(&p->rslmq, qmsg);
					} else {
						// memory error.
						nni_msg_free(qmsg);
					}
				} else {
					nni_msg *old;
					(void) nni_lmq_get(&p->rslmq, &old);
					nni_msg_free(old);
					nni_lmq_put(&p->rslmq, qmsg);
				}
			} else {
				nni_lmq_put(&p->rslmq, qmsg);
			}
		}
		ack = false;
	}

	// Store Subid RAP Topic for sub
	// TODO move to protocol layer and disconnect logic
	if (type == CMD_SUBSCRIBE && cparam->pro_ver == MQTT_VERSION_V5) {
		rv = nmq_subinfo_decode(msg, &npipe->subinfol);
		if (rv > 0)
			npipe->ntopics += rv;
	}

	// keep connection & Schedule next receive
	// nni_pipe_bump_rx(p->npipe, n);
	if (!nni_list_empty(&p->recvq)) {
		tcptran_pipe_recv_start(p);
	}
	nni_mtx_unlock(&p->mtx);

	nni_aio_set_msg(aio, msg);
	nni_aio_finish_sync(aio, 0, n);
	debug_msg("end of tcptran_pipe_recv_cb: synch! %p\n", p);
	return;

recv_error:
	nni_aio_list_remove(aio);
	msg      = p->rxmsg;
	p->rxmsg = NULL;
	nni_pipe_bump_error(p->npipe, rv);
	nni_mtx_unlock(&p->mtx);
	nni_msg_free(msg);
	nni_aio_finish_error(aio, rv);
	debug_msg("tcptran_pipe_recv_cb: recv error rv: %d\n", rv);
	return;
notify:
	// nni_pipe_bump_rx(p->npipe, n);
	nni_aio_list_remove(aio);
	// we start scheduling next receive here, however it depends on upper
	// layer tcptran_pipe_recv_start(p);
	nni_mtx_unlock(&p->mtx);
	nni_aio_set_msg(aio, NULL);
	nni_aio_finish(aio, 0, 0);
	return;
}

static void
tcptran_pipe_send_cancel(nni_aio *aio, void *arg, int rv)
{
	tcptran_pipe *p = arg;

	nni_mtx_lock(&p->mtx);
	if (!nni_aio_list_active(aio)) {
		nni_mtx_unlock(&p->mtx);
		return;
	}
	// If this is being sent, then cancel the pending transfer.
	// The callback on the txaio will cause the user aio to
	// be canceled too.
	if (nni_list_first(&p->sendq) == aio) {
		nni_aio_abort(p->txaio, rv);
		nni_mtx_unlock(&p->mtx);
		return;
	}
	nni_aio_list_remove(aio);
	nni_mtx_unlock(&p->mtx);

	nni_aio_finish_error(aio, rv);
}

/**
 * @brief send msg to V4 client
 * 
 * @param p 
 * @param msg 
 */
static inline void
nmq_pipe_send_start_v4(tcptran_pipe *p, nni_msg *msg, nni_aio *aio)
{
	nni_aio *txaio;
	int      niov;
	nni_iov  iov[3];
	uint8_t  qos;
	qos = NANO_NNI_LMQ_GET_QOS_BITS(msg);
	// qos default to 0 if the msg is not PUBLISH
	msg = NANO_NNI_LMQ_GET_MSG_POINTER(msg);
	nni_aio_set_msg(aio, msg);

	// Recomposing
	// never modify the original msg
	if (nni_msg_header_len(msg) > 0 &&
	    nni_msg_get_type(msg) == CMD_PUBLISH) {
		uint8_t      *body, *header, qos_pac;
		target_prover target_prover;
		uint8_t var_extra[2],
		    fixheader[NNI_NANO_MAX_HEADER_SIZE] = { 0 },
		    tmp[4]                              = { 0 };
		int       len_offset = 0;
		uint32_t  pos = 1;
		nni_pipe *pipe;
		uint16_t  pid;
		uint32_t  property_bytes = 0, property_len = 0;
		size_t    tlen, rlen, mlen, qlength, plength;

		pipe    = p->npipe;
		body    = nni_msg_body(msg);
		header  = nni_msg_header(msg);
		qlength = 0;
		plength = 0;
		mlen    = nni_msg_len(msg);
		qos_pac = nni_msg_get_pub_qos(msg);
		NNI_GET16(body, tlen);

		if (nni_msg_cmd_type(msg) == CMD_PUBLISH_V5) {
			// V5 to V4 shrink msg, remove property length
			// APP layer must give topic name even if topic
			// alias is set
			if (qos_pac > 0) {
				property_len = get_var_integer(
				    body + 4 + tlen, &property_bytes);

			} else {
				property_len = get_var_integer(
				    body + 2 + tlen, &property_bytes);
			}
			// V5 msg sent to V4 client
			// caculate property length and delete it
			target_prover = MQTTV5_V4;
			plength = property_len + property_bytes;
		} else if (nni_msg_cmd_type(msg) == CMD_PUBLISH) {
			target_prover = MQTTV4;
		}
		
		if (qos_pac == 0 && target_prover == MQTTV4) {
			// save time & space for QoS 0 publish
			goto send;
		}

		debug_msg("qos_pac %d sub %d\n", qos_pac, qos);
		memcpy(fixheader, header, nni_msg_header_len(msg));
		// get final qos
		qos = qos_pac > qos ? qos : qos_pac;

		// alter qos according to sub qos
		if (qos_pac > qos) {
			if (qos == 1) {
				// set qos to 1
				fixheader[0] = fixheader[0] & 0xF9;
				fixheader[0] = fixheader[0] | 0x02;
			} else {
				// set qos to 0
				fixheader[0] = fixheader[0] & 0xF9;
				len_offset   = len_offset - 2;
			}
		}
		// copy remaining length
		rlen = put_var_integer(
		    tmp, get_var_integer(header, &pos) + len_offset - plength);
		memcpy(fixheader + 1, tmp, rlen);

		txaio = p->txaio;
		niov  = 0;
		// fixed header
		qlength += rlen + 1;
		// 1st part of variable header: topic

		qlength += tlen + 2; // get topic length
		len_offset = 0;      // now use it to indicates the pid length
		// packet id
		if (qos > 0) {
			// set pid
			len_offset = 2;
			nni_msg *old;
			// packetid in aio to differ resend msg
			// TODO replace it with set prov data
			pid = nni_aio_get_packetid(aio);
			if (pid == 0) {
				// first time send this msg
				pid = nni_pipe_inc_packetid(pipe);
				// store msg for qos retrying
				nni_msg_clone(msg);
				if ((old = nni_qos_db_get(pipe->nano_qos_db,
				         pipe->p_id, pid)) != NULL) {
					// TODO packetid already exists.
					// do we need to replace old with new
					// one ? print warning to users
					nni_println(
					    "ERROR: packet id duplicates in "
					    "nano_qos_db");
					old =
					    NANO_NNI_LMQ_GET_MSG_POINTER(old);

					nni_qos_db_remove_msg(
					    pipe->nano_qos_db, old);
				}
				old = NANO_NNI_LMQ_PACKED_MSG_QOS(msg, qos);
				nni_qos_db_set(
				    pipe->nano_qos_db, pipe->p_id, pid, old);
			}
			NNI_PUT16(var_extra, pid);
			qlength += 2;
		} else if (qos_pac > 0) {
			len_offset += 2;
		}
		// use qos_buf to keep zero-copy
		if (qlength > 16 + NNI_NANO_MAX_PACKET_SIZE) {
			nng_free(p->qos_buf, 16 + NNI_NANO_MAX_PACKET_SIZE);
			p->qos_buf = nng_zalloc(sizeof(uint8_t) * (qlength));
		}

		// copy fixheader to qos_buf
		memcpy(p->qos_buf, fixheader, rlen + 1);
		// copy topic + topic length to qos_buf
		memcpy(p->qos_buf + rlen + 1, body, tlen + 2);
		if (qos > 0) {
			// copy packet id
			memcpy(p->qos_buf + rlen + tlen + 3, var_extra, 2);
		}
		iov[niov].iov_buf = p->qos_buf;
		iov[niov].iov_len = qlength;
		niov++;
		// variable header + payload
		if (mlen > 0) {
			// determine if it needs to skip packet id field
			iov[niov].iov_buf =
			    body + 2 + tlen + len_offset + plength;
			iov[niov].iov_len =
			    mlen - 2 - len_offset - tlen - plength;
			niov++;
		}

		nni_aio_set_iov(txaio, niov, iov);
		nng_stream_send(p->conn, txaio);
		p->qlength = qlength;
		return;
	}
send:
	txaio = p->txaio;
	niov  = 0;

	if (nni_msg_header_len(msg) > 0) {
		iov[niov].iov_buf = nni_msg_header(msg);
		iov[niov].iov_len = nni_msg_header_len(msg);
		niov++;
	}
	if (nni_msg_len(msg) > 0) {
		iov[niov].iov_buf = nni_msg_body(msg);
		iov[niov].iov_len = nni_msg_len(msg);
		niov++;
	}
	nni_aio_set_iov(txaio, niov, iov);
	nng_stream_send(p->conn, txaio);
}

/**
 * @brief we consider memory saving is prior to performance due
 * 	  to the requirement of our boss. so we use fragmented iov.
 * 
 * @param p 
 * @param msg 
 * @param aio 
 */
static inline void
nmq_pipe_send_start_v5(tcptran_pipe *p, nni_msg *msg, nni_aio *aio)
{
	nni_aio  *txaio;
	nni_pipe *pipe = p->npipe;
	int       niov;
	nni_iov   iov[8];

	msg = NANO_NNI_LMQ_GET_MSG_POINTER(msg);
	nni_aio_set_msg(aio, msg);

	if (nni_msg_get_type(msg) != CMD_PUBLISH)
		goto send;
	// never modify the original msg

	uint8_t      *body, *header, qos_pac;
	target_prover target_prover;
	int           len_offset = 0, sub_id = 0, qos;
	uint16_t      pid;
	uint32_t tprop_bytes, prop_bytes = 0, id_bytes = 0, property_len = 0;
	size_t   tlen, rlen, mlen, hlen, qlength, plength;

	txaio   = p->txaio;
	body    = nni_msg_body(msg);
	header  = nni_msg_header(msg);
	niov    = 0;
	qlength = 0;
	plength = 0;
	mlen    = nni_msg_len(msg);
	hlen    = nni_msg_header_len(msg);
	qos_pac = nni_msg_get_pub_qos(msg);
	NNI_GET16(body, tlen);

	// check max packet size for this client/msg
	uint32_t total_len = mlen + hlen;
	if (total_len > p->tcp_cparam->max_packet_size) {
		// drop msg and finish aio
		// pretend it has been sent
		debug_syslog("Warning:msg dropped due to overceed max packet size!");
		nni_msg_free(msg);
		nni_aio_set_msg(aio, NULL);
		nni_aio_finish(aio, 0, 0);
		return;
	}
	if (nni_msg_cmd_type(msg) == CMD_PUBLISH_V5) {
		if (qos_pac > 0) {
			property_len =
			    get_var_integer(body + 4 + tlen, &prop_bytes);
		} else {
			property_len =
			    get_var_integer(body + 2 + tlen, &prop_bytes);
		}
		target_prover = MQTTV5;
		tprop_bytes = prop_bytes;
	}
	// subid
	subinfo *info, *tinfo;
	tinfo = nni_aio_get_prov_data(txaio);
	nni_aio_set_prov_data(txaio, NULL);
	NNI_LIST_FOREACH (&p->npipe->subinfol, info) {
		if (tinfo != NULL && info != tinfo ) {
			continue;
		}
		tinfo = NULL;
		len_offset=0;
		if (topic_filtern(info->topic, (char*)(body + 2), tlen)) {
			if (niov >= 8) {
				// nng aio only allow 2 msgs at a time
				nni_aio_set_prov_data(txaio, info);
				break;
			}
			uint8_t  var_extra[2], fixheader, tmp[4] = { 0 };
			uint8_t  proplen[4] = { 0 }, var_subid[5] = { 0 };
			uint32_t pos = 1;
			sub_id       = info->subid;
			qos          = info->qos;
			if (nni_msg_cmd_type(msg) == CMD_PUBLISH) {
				// V4 to V5 add 0 property length
				target_prover = MQTTV4_V5;
				prop_bytes    = 1;
				tprop_bytes   = 1;
				len_offset    = 1;
			}
			if (info->rap == 0) {
				*header = *header & 0xFE;
			}
			if (sub_id != 0) {
				var_subid[0] = 0x0B;
				id_bytes = put_var_integer(var_subid+1, sub_id);
				tprop_bytes = put_var_integer(proplen, property_len+1+id_bytes);
				len_offset += (tprop_bytes - prop_bytes + 1 + id_bytes);
			}
			//else use original var payload & pid
			fixheader = *header;
			// get final qos
			qos = qos_pac > qos ? qos : qos_pac;

			// alter qos according to sub qos
			if (qos_pac > qos) {
				if (qos == 1) {
					// set qos to 1
					fixheader = fixheader & 0xF9;
					fixheader = fixheader | 0x02;
				} else {
					// set qos to 0
					fixheader = fixheader & 0xF9;
					len_offset   = len_offset - 2;
				}
			}
			// fixed header + remaining length
			pos=1;
			rlen = put_var_integer(
			    tmp, get_var_integer(header, &pos) + len_offset);
			// or just copy to qosbuf directly?
			*(p->qos_buf + qlength) = fixheader;
			memcpy(p->qos_buf + qlength + 1, tmp, rlen);
			iov[niov].iov_buf = p->qos_buf + qlength;
			iov[niov].iov_len = rlen + 1;
			niov++;
			qlength += rlen + 1;
			// 1st part of variable header: topic + topic len
			iov[niov].iov_buf = body;
			iov[niov].iov_len = tlen+2;
			niov++;
			// len to indicate the offset in packet
			len_offset = 0;
			plength = 0;
			if (qos > 0) {
				// set pid
				len_offset = 2;
				nni_msg *old;
				// packetid in aio to differ resend msg
				// TODO replace it with set prov data
				pid = nni_aio_get_packetid(aio);
				if (pid == 0) {
					// first time send this msg
					pid = nni_pipe_inc_packetid(pipe);
					// store msg for qos retrying
					nni_msg_clone(msg);
					if ((old = nni_qos_db_get(
					         pipe->nano_qos_db, pipe->p_id,
					         pid)) != NULL) {
						// TODO packetid already
						// exists. do we need to
						// replace old with new one ?
						// print warning to users
						nni_println("ERROR: packet id "
						            "duplicates in "
						            "nano_qos_db");
						old =
						    NANO_NNI_LMQ_GET_MSG_POINTER(
						        old);

						nni_qos_db_remove_msg(
						    pipe->nano_qos_db, old);
					}
					old = NANO_NNI_LMQ_PACKED_MSG_QOS(
					    msg, qos);
					nni_qos_db_set(pipe->nano_qos_db,
					    pipe->p_id, pid, old);
				}
				NNI_PUT16(var_extra, pid);
				// copy packet id
				memcpy(p->qos_buf + qlength, var_extra, 2);
				qlength += 2;
				plength += 2;
			} else if (qos_pac > 0) {
				//ignore the packet id of original packet
				len_offset += 2;
			}
			// prop len + sub id if any
			if (sub_id != 0) {
				memcpy(p->qos_buf + qlength, proplen,
				    tprop_bytes);
				qlength += tprop_bytes;
				plength += tprop_bytes;
				memcpy(p->qos_buf + qlength, var_subid,
				    id_bytes + 1);
				qlength += id_bytes + 1;
				plength += id_bytes + 1;
				if (target_prover == MQTTV5)
					len_offset += prop_bytes;
			} else {
				//need to add 0 len for V4 msg
				if (target_prover == MQTTV4_V5) {
					// add proplen even 0
					memcpy(p->qos_buf + qlength, proplen,
					    tprop_bytes);
					qlength += tprop_bytes;
					plength += tprop_bytes;
				}
			}
			// 2nd part of variable header: pid + proplen+0x0B+subid
			iov[niov].iov_buf = p->qos_buf+qlength-plength;
			iov[niov].iov_len = plength;
			niov++;
			// prop + body
			iov[niov].iov_buf = body + 2 + tlen + len_offset;
			iov[niov].iov_len = mlen - 2 - len_offset - tlen;
			niov++;
		}
	}

	// MQTT V5 flow control
	if (qos > 0) {
		if (p->qsend_quota > 0) {
			p->qsend_quota--;
		} else {
			// what should broker does when exceed
			// max_recv? msg lost, make it look like a
			// normal send. qos msg will be resend
			// afterwards
			nni_msg_free(msg);
			nni_aio_set_prov_data(txaio, NULL);
			nni_aio_set_msg(aio, NULL);
			nni_aio_finish(aio, 0, 0);
			return;
		}
	}
	nni_aio_set_iov(txaio, niov, iov);
	nng_stream_send(p->conn, txaio);
	return;

send:
	txaio = p->txaio;
	niov  = 0;

	if (nni_msg_header_len(msg) > 0) {
		iov[niov].iov_buf = nni_msg_header(msg);
		iov[niov].iov_len = nni_msg_header_len(msg);
		niov++;
	}
	if (nni_msg_len(msg) > 0) {
		iov[niov].iov_buf = nni_msg_body(msg);
		iov[niov].iov_len = nni_msg_len(msg);
		niov++;
	}
	nni_aio_set_iov(txaio, niov, iov);
	nng_stream_send(p->conn, txaio);
}

/**
 * @brief this is the func that responsible for sending msg while
 *        keeping zero-copy feature, doing all the jobs neccesary
 *        for each unique client (it means ugly)
 * //TODO break it into 2 pieces V4 / V5
 *
 * @param p tcptran_pipe
 */
static void
tcptran_pipe_send_start(tcptran_pipe *p)
{
	nni_aio *aio;
	nni_msg *msg;

	debug_msg("########### tcptran_pipe_send_start ###########");
	if (p->closed) {
		while ((aio = nni_list_first(&p->sendq)) != NULL) {
			nni_list_remove(&p->sendq, aio);
			nni_aio_finish_error(aio, NNG_ECLOSED);
		}
		return;
	}

	if ((aio = nni_list_first(&p->sendq)) == NULL) {
		debug_msg("aio not functioning");
		return;
	}

	// This runs to send the message.
	msg = nni_aio_get_msg(aio);
	if (msg == NULL || p->tcp_cparam == NULL) {
		// TODO error handler
		nni_println("ERROR: sending NULL msg or pipe is invalid!");
		nni_aio_finish(aio, NNG_ECANCELED, 0);
		return;
	}
	if (p->tcp_cparam->pro_ver == 4) {
		nmq_pipe_send_start_v4(p, msg, aio);
		return;
	} else if (p->tcp_cparam->pro_ver == 5) {
		nmq_pipe_send_start_v5(p, msg, aio);
		return;
	}
}

static void
tcptran_pipe_send(void *arg, nni_aio *aio)
{
	tcptran_pipe *p = arg;
	int           rv;

	debug_msg("####################tcptran_pipe_send###########");
	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&p->mtx);
	if ((rv = nni_aio_schedule(aio, tcptran_pipe_send_cancel, p)) != 0) {
		nni_mtx_unlock(&p->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_list_append(&p->sendq, aio);
	if (nni_list_first(&p->sendq) == aio) {
		// send publish msg or send others
		tcptran_pipe_send_start(p);
	}
	nni_mtx_unlock(&p->mtx);
}

static void
tcptran_pipe_recv_cancel(nni_aio *aio, void *arg, int rv)
{
	tcptran_pipe *p = arg;

	nni_mtx_lock(&p->mtx);
	if (!nni_aio_list_active(aio)) {
		nni_mtx_unlock(&p->mtx);
		return;
	}
	// If receive in progress, then cancel the pending transfer.
	// The callback on the rxaio will cause the user aio to
	// be canceled too.
	if (nni_list_first(&p->recvq) == aio) {
		nni_aio_abort(p->rxaio, rv);
		nni_mtx_unlock(&p->mtx);
		return;
	}
	nni_aio_list_remove(aio);
	nni_mtx_unlock(&p->mtx);
	nni_aio_finish_error(aio, rv);
}

static void
tcptran_pipe_recv(void *arg, nni_aio *aio)
{
	tcptran_pipe *p = arg;
	int           rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&p->mtx);
	if ((rv = nni_aio_schedule(aio, tcptran_pipe_recv_cancel, p)) != 0) {
		nni_mtx_unlock(&p->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}

	if (nni_aio_list_active(aio) == 0) {
		nni_list_append(&p->recvq, aio);
	}

	if (nni_list_first(&p->recvq) == aio) {
		tcptran_pipe_recv_start(p);
	}
	nni_mtx_unlock(&p->mtx);
}

/*
static uint16_t
tcptran_pipe_peer(void *arg)
{
        tcptran_pipe *p = arg;

        return (p->peer);
}
*/

static int
tcptran_pipe_getopt(
    void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	tcptran_pipe *p = arg;
	return (nni_stream_get(p->conn, name, buf, szp, t));
}

static void
tcptran_pipe_recv_start(tcptran_pipe *p)
{
	nni_aio *rxaio;
	nni_iov  iov;
	debug_msg("*** tcptran_pipe_recv_start ***\n");
	NNI_ASSERT(p->rxmsg == NULL);

	if (p->closed) {
		nni_aio *aio;
		while ((aio = nni_list_first(&p->recvq)) != NULL) {
			nni_list_remove(&p->recvq, aio);
			nni_aio_finish_error(aio, NNG_ECLOSED);
		}
		return;
	}
	if (nni_list_empty(&p->recvq)) {
		return;
	}

	// Schedule a read of the fixed header.
	rxaio         = p->rxaio;
	p->gotrxhead  = 0;
	p->wantrxhead = NANO_MIN_FIXED_HEADER_LEN;
	iov.iov_buf   = p->rxlen;
	iov.iov_len   = NANO_MIN_FIXED_HEADER_LEN;
	nni_aio_set_iov(rxaio, 1, &iov);
	nng_stream_recv(p->conn, rxaio);
}

// DEAL WITH CONNECT when PIPE INIT
static void
tcptran_pipe_start(tcptran_pipe *p, nng_stream *conn, tcptran_ep *ep)
{
	nni_iov iov;
	// nni_tcp_conn *c;

	ep->refcnt++;

	p->conn = conn;
	p->ep   = ep;
	// p->proto = ep->proto;

	debug_msg("tcptran_pipe_start!");
	p->qrecv_quota = NANO_MAX_QOS_PACKET;
	p->gotrxhead   = 0;
	p->wantrxhead  = NANO_CONNECT_PACKET_LEN; // packet type 1 + remaining
	                                          // length 1 + protocal name 7
	// + flag 1 + keepalive 2 = 12
	iov.iov_len = NNI_NANO_MAX_HEADER_SIZE; // dynamic
	iov.iov_buf = p->rxlen;

	nni_aio_set_iov(p->negoaio, 1, &iov);
	nni_list_append(&ep->negopipes, p);

	nni_aio_set_timeout(p->negoaio,
	    15 * 1000); // 15 sec timeout to negotiate abide with emqx

	nng_stream_recv(p->conn, p->negoaio);
}

static void
tcptran_ep_fini(void *arg)
{
	tcptran_ep *ep = arg;

	nni_mtx_lock(&ep->mtx);
	ep->fini = true;
	if (ep->refcnt != 0) {
		nni_mtx_unlock(&ep->mtx);
		return;
	}
	nni_mtx_unlock(&ep->mtx);
	nni_aio_stop(ep->timeaio);
	nni_aio_stop(ep->connaio);
	nng_stream_listener_free(ep->listener);
	nni_aio_free(ep->timeaio);
	nni_aio_free(ep->connaio);

	nni_mtx_fini(&ep->mtx);
	NNI_FREE_STRUCT(ep);
}

static void
tcptran_ep_close(void *arg)
{
	tcptran_ep   *ep = arg;
	tcptran_pipe *p;

	nni_mtx_lock(&ep->mtx);

	debug_syslog("tcptran_ep_close");
	ep->closed = true;
	nni_aio_close(ep->timeaio);
	if (ep->listener != NULL) {
		nng_stream_listener_close(ep->listener);
	}
	NNI_LIST_FOREACH (&ep->negopipes, p) {
		tcptran_pipe_close(p);
	}
	NNI_LIST_FOREACH (&ep->waitpipes, p) {
		tcptran_pipe_close(p);
	}
	NNI_LIST_FOREACH (&ep->busypipes, p) {
		tcptran_pipe_close(p);
	}
	if (ep->useraio != NULL) {
		nni_aio_finish_error(ep->useraio, NNG_ECLOSED);
		ep->useraio = NULL;
	}

	nni_mtx_unlock(&ep->mtx);
}

// This parses off the optional source address that this transport uses.
// The special handling of this URL format is quite honestly an historical
// mistake, which we would remove if we could.static int
int
tcptran_url_parse_source(nng_url *url, nng_sockaddr *sa, const nng_url *surl)
{
	int      af;
	char    *semi;
	char    *src;
	size_t   len;
	int      rv;
	nni_aio *aio;

	// We modify the URL.  This relies on the fact that the underlying
	// transport does not free this, so we can just use references.

	url->u_scheme   = surl->u_scheme;
	url->u_port     = surl->u_port;
	url->u_hostname = surl->u_hostname;

	if ((semi = strchr(url->u_hostname, ';')) == NULL) {
		memset(sa, 0, sizeof(*sa));
		return (0);
	}

	len             = (size_t) (semi - url->u_hostname);
	url->u_hostname = semi + 1;

	if (strcmp(surl->u_scheme, "tcp") == 0) {
		af = NNG_AF_UNSPEC;
	} else if (strcmp(surl->u_scheme, "tcp4") == 0) {
		af = NNG_AF_INET;
	} else if (strcmp(surl->u_scheme, "tcp6") == 0) {
		af = NNG_AF_INET6;
	} else {
		return (NNG_EADDRINVAL);
	}

	if ((src = nni_alloc(len + 1)) == NULL) {
		return (NNG_ENOMEM);
	}
	memcpy(src, surl->u_hostname, len);
	src[len] = '\0';

	if ((rv = nni_aio_alloc(&aio, NULL, NULL)) != 0) {
		nni_free(src, len + 1);
		return (rv);
	}

	nni_resolv_ip(src, "0", af, true, sa, aio);
	nni_aio_wait(aio);
	rv = nni_aio_result(aio);
	nni_aio_free(aio);
	nni_free(src, len + 1);
	return (rv);
}

static void
tcptran_timer_cb(void *arg)
{
	tcptran_ep *ep = arg;
	if (nni_aio_result(ep->timeaio) == 0) {
		nng_stream_listener_accept(ep->listener, ep->connaio);
	}
}

// TCP accpet trigger
static void
tcptran_accept_cb(void *arg)
{
	tcptran_ep   *ep  = arg;
	nni_aio      *aio = ep->connaio;
	tcptran_pipe *p;
	int           rv;
	nng_stream   *conn;

	nni_mtx_lock(&ep->mtx);

	if ((rv = nni_aio_result(aio)) != 0) {
		goto error;
	}

	conn = nni_aio_get_output(aio, 0);
	if ((rv = tcptran_pipe_alloc(&p)) != 0) {
		nng_stream_free(conn);
		goto error;
	}

	if (ep->closed) {
		tcptran_pipe_fini(p);
		nng_stream_free(conn);
		rv = NNG_ECLOSED;
		goto error;
	}
	tcptran_pipe_start(p, conn, ep);
	nng_stream_listener_accept(ep->listener, ep->connaio);
	nni_mtx_unlock(&ep->mtx);
	return;

error:
	// When an error here occurs, let's send a notice up to the consumer.
	// That way it can be reported properly.
	if ((aio = ep->useraio) != NULL) {
		ep->useraio = NULL;
		nni_aio_finish_error(aio, rv);
	}
	switch (rv) {

	case NNG_ENOMEM:
	case NNG_ENOFILES:
		nng_sleep_aio(10, ep->timeaio);
		break;

	default:
		if (!ep->closed) {
			nng_stream_listener_accept(ep->listener, ep->connaio);
		}
		break;
	}
	nni_mtx_unlock(&ep->mtx);
}

static int
tcptran_ep_init(tcptran_ep **epp, nng_url *url, nni_sock *sock)
{
	tcptran_ep *ep;
	NNI_ARG_UNUSED(sock);

	if ((ep = NNI_ALLOC_STRUCT(ep)) == NULL) {
		return (NNG_ENOMEM);
	}
	nni_mtx_init(&ep->mtx);
	NNI_LIST_INIT(&ep->busypipes, tcptran_pipe, node);
	NNI_LIST_INIT(&ep->waitpipes, tcptran_pipe, node);
	NNI_LIST_INIT(&ep->negopipes, tcptran_pipe, node);

	// ep->proto = nni_sock_proto_id(sock);
	ep->url = url;
#ifdef NNG_ENABLE_STATS
	static const nni_stat_info rcv_max_info = {
		.si_name   = "rcv_max",
		.si_desc   = "maximum receive size",
		.si_type   = NNG_STAT_LEVEL,
		.si_unit   = NNG_UNIT_BYTES,
		.si_atomic = true,
	};
	nni_stat_init(&ep->st_rcv_max, &rcv_max_info);
#endif

	*epp = ep;
	return (0);
}

static int
tcptran_listener_init(void **lp, nng_url *url, nni_listener *nlistener)
{
	tcptran_ep *ep;
	int         rv;
	nni_sock   *sock = nni_listener_sock(nlistener);

	// Check for invalid URL components.
	if ((strlen(url->u_path) != 0) && (strcmp(url->u_path, "/") != 0)) {
		return (NNG_EADDRINVAL);
	}
	if ((url->u_fragment != NULL) || (url->u_userinfo != NULL) ||
	    (url->u_query != NULL)) {
		return (NNG_EADDRINVAL);
	}

	if ((rv = tcptran_ep_init(&ep, url, sock)) != 0) {
		return (rv);
	}

	if (((rv = nni_aio_alloc(&ep->connaio, tcptran_accept_cb, ep)) != 0) ||
	    ((rv = nni_aio_alloc(&ep->timeaio, tcptran_timer_cb, ep)) != 0) ||
	    ((rv = nng_stream_listener_alloc_url(&ep->listener, url)) != 0)) {
		tcptran_ep_fini(ep);
		return (rv);
	}

#ifdef NNG_ENABLE_STATS
	nni_listener_add_stat(nlistener, &ep->st_rcv_max);
#endif
	*lp = ep;
	return (0);
}

static void
tcptran_ep_cancel(nni_aio *aio, void *arg, int rv)
{
	tcptran_ep *ep = arg;
	nni_mtx_lock(&ep->mtx);
	if (ep->useraio == aio) {
		ep->useraio = NULL;
		nni_aio_finish_error(aio, rv);
	}
	nni_mtx_unlock(&ep->mtx);
}

// TODO network interface bind
static int
tcptran_ep_get_url(void *arg, void *v, size_t *szp, nni_opt_type t)
{
	tcptran_ep *ep = arg;
	char       *s;
	int         rv;
	int         port = 0;

	if (ep->listener != NULL) {
		(void) nng_stream_listener_get_int(
		    ep->listener, NNG_OPT_TCP_BOUND_PORT, &port);
	}

	if ((rv = nni_url_asprintf_port(&s, ep->url, port)) == 0) {
		rv = nni_copyout_str(s, v, szp, t);
		nni_strfree(s);
	}
	return (rv);
}

static int
tcptran_ep_get_recvmaxsz(void *arg, void *v, size_t *szp, nni_opt_type t)
{
	tcptran_ep *ep = arg;
	int         rv;

	nni_mtx_lock(&ep->mtx);
	rv = nni_copyout_size(ep->rcvmax, v, szp, t);
	nni_mtx_unlock(&ep->mtx);
	return (rv);
}

static int
tcptran_ep_set_recvmaxsz(void *arg, const void *v, size_t sz, nni_opt_type t)
{
	tcptran_ep *ep = arg;
	size_t      val;
	int         rv;
	if ((rv = nni_copyin_size(&val, v, sz, 0, NNI_MAXSZ, t)) == 0) {
		tcptran_pipe *p;
		nni_mtx_lock(&ep->mtx);
		ep->rcvmax = val;
		NNI_LIST_FOREACH (&ep->waitpipes, p) {
			p->rcvmax = val;
		}
		NNI_LIST_FOREACH (&ep->negopipes, p) {
			p->rcvmax = val;
		}
		NNI_LIST_FOREACH (&ep->busypipes, p) {
			p->rcvmax = val;
		}
		nni_mtx_unlock(&ep->mtx);
#ifdef NNG_ENABLE_STATS
		nni_stat_set_value(&ep->st_rcv_max, val);
#endif
	}
	return (rv);
}

static int
tcptran_ep_bind(void *arg)
{
	tcptran_ep *ep = arg;
	int         rv;

	nni_mtx_lock(&ep->mtx);
	rv = nng_stream_listener_listen(ep->listener);
	nni_mtx_unlock(&ep->mtx);

	return (rv);
}

static void
tcptran_ep_accept(void *arg, nni_aio *aio)
{
	tcptran_ep *ep = arg;
	int         rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&ep->mtx);
	if (ep->closed) {
		nni_mtx_unlock(&ep->mtx);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}
	if (ep->useraio != NULL) {
		nni_mtx_unlock(&ep->mtx);
		nni_aio_finish_error(aio, NNG_EBUSY);
		return;
	}
	if ((rv = nni_aio_schedule(aio, tcptran_ep_cancel, ep)) != 0) {
		nni_mtx_unlock(&ep->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	ep->useraio = aio;
	if (!ep->started) {
		ep->started = true;
		nng_stream_listener_accept(ep->listener, ep->connaio);
	} else {
		tcptran_ep_match(ep);
	}
	nni_mtx_unlock(&ep->mtx);
}

static nni_sp_pipe_ops tcptran_pipe_ops = {
	.p_init  = tcptran_pipe_init,
	.p_fini  = tcptran_pipe_fini,
	.p_stop  = tcptran_pipe_stop,
	.p_send  = tcptran_pipe_send,
	.p_recv  = tcptran_pipe_recv,
	.p_close = tcptran_pipe_close,
	//.p_peer   = tcptran_pipe_peer,
	.p_getopt = tcptran_pipe_getopt,
};

static const nni_option tcptran_ep_opts[] = {
	{
	    .o_name = NNG_OPT_RECVMAXSZ,
	    .o_get  = tcptran_ep_get_recvmaxsz,
	    .o_set  = tcptran_ep_set_recvmaxsz,
	},
	{
	    .o_name = NNG_OPT_URL,
	    .o_get  = tcptran_ep_get_url,
	},
	// terminate list
	{
	    .o_name = NULL,
	},
};

static int
tcptran_listener_getopt(
    void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	tcptran_ep *ep = arg;
	int         rv;

	rv = nni_stream_listener_get(ep->listener, name, buf, szp, t);
	if (rv == NNG_ENOTSUP) {
		rv = nni_getopt(tcptran_ep_opts, name, ep, buf, szp, t);
	}
	return (rv);
}

static int
tcptran_listener_setopt(
    void *arg, const char *name, const void *buf, size_t sz, nni_type t)
{
	tcptran_ep *ep = arg;
	int         rv;

	rv = nni_stream_listener_set(ep->listener, name, buf, sz, t);
	if (rv == NNG_ENOTSUP) {
		rv = nni_setopt(tcptran_ep_opts, name, ep, buf, sz, t);
	}
	return (rv);
}

static nni_sp_listener_ops tcptran_listener_ops = {
	.l_init   = tcptran_listener_init,
	.l_fini   = tcptran_ep_fini,
	.l_bind   = tcptran_ep_bind,
	.l_accept = tcptran_ep_accept,
	.l_close  = tcptran_ep_close,
	.l_getopt = tcptran_listener_getopt,
	.l_setopt = tcptran_listener_setopt,
};

static nni_sp_tran tcp__tran_mqtt = {
	.tran_scheme   = "broker+tcp",
	.tran_listener = &tcptran_listener_ops,
	.tran_pipe     = &tcptran_pipe_ops,
	.tran_init     = tcptran_init,
	.tran_fini     = tcptran_fini,
};

static nni_sp_tran tcp4__tran_mqtt = {
	.tran_scheme   = "broker+tcp4",
	.tran_listener = &tcptran_listener_ops,
	.tran_pipe     = &tcptran_pipe_ops,
	.tran_init     = tcptran_init,
	.tran_fini     = tcptran_fini,
};

static nni_sp_tran tcp6__tran_mqtt = {
	.tran_scheme   = "broker+tcp6",
	.tran_listener = &tcptran_listener_ops,
	.tran_pipe     = &tcptran_pipe_ops,
	.tran_init     = tcptran_init,
	.tran_fini     = tcptran_fini,
};

static nni_sp_tran tcp_tran_mqtt = {
	.tran_scheme   = "nmq-tcp",
	.tran_listener = &tcptran_listener_ops,
	.tran_pipe     = &tcptran_pipe_ops,
	.tran_init     = tcptran_init,
	.tran_fini     = tcptran_fini,
};

static nni_sp_tran tcp4_tran_mqtt = {
	.tran_scheme   = "nmq-tcp4",
	.tran_listener = &tcptran_listener_ops,
	.tran_pipe     = &tcptran_pipe_ops,
	.tran_init     = tcptran_init,
	.tran_fini     = tcptran_fini,
};

static nni_sp_tran tcp6_tran_mqtt = {
	.tran_scheme   = "nmq-tcp6",
	.tran_listener = &tcptran_listener_ops,
	.tran_pipe     = &tcptran_pipe_ops,
	.tran_init     = tcptran_init,
	.tran_fini     = tcptran_fini,
};

#ifndef NNG_ELIDE_DEPRECATED
int
nmq_mqtt_tcp_register(void)
{
	return (nni_init());
}
#endif

void
nni_nmq_broker_tcp_register(void)
{
	nni_sp_tran_register(&tcp_tran_mqtt);
	nni_sp_tran_register(&tcp4_tran_mqtt);
	nni_sp_tran_register(&tcp6_tran_mqtt);
	nni_sp_tran_register(&tcp__tran_mqtt);
	nni_sp_tran_register(&tcp4__tran_mqtt);
	nni_sp_tran_register(&tcp6__tran_mqtt);
}
