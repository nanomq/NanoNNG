//
// Copyright 2025 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// MQTT over WebSocket dosen't enjoy same security fix and performance
// enhancement as tls/tcp. This could be brought down by malformed packets
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nng_impl.h"
#include "core/sockimpl.h"
#include "supplemental/websocket/websocket.h"

#include "nng/transport/mqttws/nmq_websocket.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"

#include "nng/supplemental/tls/tls.h"
#include "nng/supplemental/nanolib/mqtt_db.h"
#include "nng/supplemental/nanolib/conf.h"
#include "supplemental/mqtt/mqtt_qos_db_api.h"
#include "supplemental/mqtt/mqtt_msg.h"

typedef struct ws_listener ws_listener;
typedef struct ws_pipe     ws_pipe;

struct ws_listener {
	uint16_t             peer; // remote protocol
	conf                *conf;
	nni_list             aios;
	nni_mtx              mtx;
	nni_aio             *accaio;
	nng_stream_listener *listener;
	bool                 started;
};

struct ws_pipe {
	nni_mtx     mtx;
	bool        closed;
	uint8_t     txlen[NANO_MIN_PACKET_LEN];
	uint16_t    peer;
	size_t      gotrxhead;
	size_t      wantrxhead;
	nni_lmq     recvlmq;
	nni_lmq     rslmq;	// Only for QoS msg ack cache
	conf       *conf;
	nni_msg    *tmp_msg;	// Serving as recv buffer, convergence all msg from nng ws
	nni_aio    *user_txaio;
	nni_aio    *user_rxaio;
	nni_aio    *ep_aio;
	nni_aio    *txaio;
	nni_aio    *rxaio;
	nni_aio    *qsaio;
	nni_pipe   *npipe;
	conn_param *ws_param;
	nng_stream *ws;
	uint8_t    *qos_buf; // msg trunk for qos & V4/V5 conversion
	size_t      qlength; // length of qos_buf
	// MQTT V5
	uint16_t    qrecv_quota;	//Not valid yet, due to NNG websocket limitation
	uint32_t    qsend_quota;
	reason_code err_code; // work with closed flag
};

static void wstran_pipe_close(void *arg);

static void
wstran_pipe_send_cb(void *arg)
{
	ws_pipe *p = arg;
	nni_aio *taio;
	nni_aio *uaio;

	nni_mtx_lock(&p->mtx);
	taio          = p->txaio;
	uaio          = p->user_txaio;
	p->user_txaio = NULL;

	if (uaio != NULL) {
		if (p->closed){
			nni_aio_finish_error(uaio, p->err_code);
			nni_mtx_unlock(&p->mtx);
			return;
		}
		int rv;
		if ((rv = nni_aio_result(taio)) != 0) {
			nni_aio_finish_error(uaio, rv);
		} else {
			nni_aio_finish(uaio, 0, 0);
		}
	}
	nni_mtx_unlock(&p->mtx);
}

static void
wstran_pipe_qos_send_cb(void *arg)
{
	int rv;
	nni_msg *qmsg;
	ws_pipe *p     = arg;
	nni_aio *qsaio = p->qsaio;
	nni_aio *uaio  = p->user_txaio;

	if ((rv = nni_aio_result(qsaio)) != 0) {
		log_warn(" send aio error %s", nng_strerror(rv));
		nni_msg *msg;
		if ((msg = nni_aio_get_msg(p->qsaio)) != NULL) {
			nni_msg_free(msg);
		}
		if (uaio != NULL) {
			nni_aio_finish_error(uaio, rv);
		}
	}

	nni_mtx_lock(&p->mtx);
	if (nni_lmq_get(&p->rslmq, &qmsg) == 0) {
		nni_iov iov[2];
		iov[0].iov_len = nni_msg_header_len(qmsg);
		iov[0].iov_buf = nni_msg_header(qmsg);
		iov[1].iov_len = nni_msg_len(qmsg);
		iov[1].iov_buf = nni_msg_body(qmsg);
		nni_aio_set_msg(p->qsaio, qmsg);
		// send ACK down...
		nni_aio_set_iov(p->qsaio, 2, iov);
		nng_stream_send(p->ws, p->qsaio);
	}
	nni_mtx_unlock(&p->mtx);
	return;
}

static void
wstran_pipe_recv_cb(void *arg)
{
	ws_pipe *p = arg;
	nni_iov  iov[2];
	uint8_t  rv, count = 0, pos = 1;
	uint64_t len = 0;
	uint8_t *ptr;
	nni_msg *smsg = NULL, *msg = NULL;
	nni_msg **msg_vec = NULL;
	nni_aio *raio = p->rxaio;
	nni_aio *uaio = NULL;

	nni_mtx_lock(&p->mtx);
	p->err_code = MQTT_SUCCESS;
	// only sets uaio at first time
	if (p->user_rxaio != NULL) {
		uaio = p->user_rxaio;
	}
	// process scatterd msgs
	if ((rv = nni_aio_result(raio)) != 0) {
		log_warn(" recv aio error %s", nng_strerror(rv));
		goto reset;
	}
	msg = nni_aio_get_msg(raio);
	if (nni_msg_header_len(msg) == 0 && nni_msg_len(msg) == 0) {
		nni_msg_free(msg);
		log_trace("empty msg received! continue next receive");
		goto recv;
	}
	ptr = nni_msg_body(msg);
	p->gotrxhead += nni_msg_len(msg);
	log_debug("#### wstran_pipe_recv_cb got %ld msg: %p %x %ld",
	    p->gotrxhead, ptr, *ptr, nni_msg_len(msg));
	// first we collect complete Fixheader
	if (p->tmp_msg == NULL && p->gotrxhead > 0) {
		if ((rv = nni_msg_alloc(&p->tmp_msg, 0)) != 0) {
			log_error("mem error %ld\n", (size_t) len);
			goto reset;
		}
	}

	nni_msg_append(p->tmp_msg, ptr, nni_msg_len(msg));
	ptr = nni_msg_body(p->tmp_msg); // packet might be sticky?

	if (p->wantrxhead == 0) {
		if (p->gotrxhead == 1) {
			goto recv;
		}
		len = get_var_integer(ptr, &pos);
		if (*(ptr + pos - 1) > 0x7f) {
			// continue to next byte of remaining length
			if (p->gotrxhead >= NNI_NANO_MAX_HEADER_SIZE) {
				// length error
				rv = NNG_EMSGSIZE;
				goto reset;
			}
			goto recv;
		} else {
			// Fixed header finished
			p->wantrxhead = len + pos;
			nni_msg_set_cmd_type(p->tmp_msg, *ptr & 0xf0);
		}
	}
	if (p->wantrxhead > p->gotrxhead)
		goto recv;
	// Negotiation shall be processed alone
	if ((*ptr & 0xF0) == CMD_CONNECT) {
		goto done;
	}

	size_t index = 0;
	pos = 1;
	while (p->gotrxhead > index) {
		len = get_var_integer(ptr, &pos);
		// Fixed header finished
		index += len + pos;
		nni_msg *new;
		count ++;
		if (nni_msg_alloc(&new, 0) != 0) {
			p->err_code = SERVER_UNAVAILABLE;
			goto skip;
		}
		// parse fixed header
		ws_msg_adaptor(ptr, new);
		nni_msg_set_conn_param(new, p->ws_param);
		if (smsg == NULL) {
			smsg = new;
		} else {
			nni_lmq_put(&p->recvlmq, new);
		}
		cvector_push_back(msg_vec, new);
		ptr = ptr + len + pos;
		pos = 1;
	}
	if (p->gotrxhead < index) {
		log_error("Decoding error!");
		goto skip;
	} else if (p->gotrxhead == index) {
		goto done;
	}
recv:
	nni_msg_free(msg);
	msg = NULL;
	nng_stream_recv(p->ws, raio);
	nni_mtx_unlock(&p->mtx);
	return;
done:
	nni_msg_free(msg);
	msg = NULL;
	if (uaio == NULL) {
		// Processing CONNECT
		uaio = p->ep_aio;
	}
	if (uaio == NULL) {
		log_warn("No AIO waitting, unable to process websocket packet");
		goto reset;
	}
	if (p->gotrxhead+p->wantrxhead > p->conf->max_packet_size) {
		log_trace("size error 0x95\n");
		rv = NMQ_PACKET_TOO_LARGE;
		p->err_code = NMQ_PACKET_TOO_LARGE;
		goto skip;
	}
	p->gotrxhead  = 0;
	p->wantrxhead = 0;
	if (nni_msg_cmd_type(p->tmp_msg) == CMD_CONNECT) {
		// end of nego
		if (p->ws_param == NULL) {
			conn_param_alloc(&p->ws_param);
		}
		// CONNECT shall be sent & decode alone, or is a bug from Client
		if (conn_handler(nni_msg_body(p->tmp_msg), p->ws_param,
		        nni_msg_len(p->tmp_msg)) != 0) {
			p->closed = true;
			p->err_code = PROTOCOL_ERROR;
			goto skip;
		}
		log_trace("MQTT Clientid is %s", p->ws_param->clientid.body);
		if (p->ws_param->pro_ver == 5) {
			p->qsend_quota = p->ws_param->rx_max;
		}
		if (p->ws_param->max_packet_size == 0) {
			// set default max packet size for client
			p->ws_param->max_packet_size =
			    p->conf->client_max_packet_size;
		}
		log_info("max_packet_size of %.*s is %d",
				p->ws_param->clientid.len, p->ws_param->clientid.body,
				p->ws_param->max_packet_size);
		nni_msg_free(p->tmp_msg);
		p->tmp_msg = NULL;
		nni_aio_set_output(uaio, 0, p);
		// pipe_start_cb send CONNACK
		nni_aio_finish(uaio, 0, 0);
		nni_mtx_unlock(&p->mtx);
		return;
	}
	nni_msg_free(p->tmp_msg);
	p->tmp_msg = NULL;
	if (p->ws_param == NULL) {
		log_warn("Malformed Packet!");
		goto reset;
	}
	for (size_t i = 0; i < cvector_size(msg_vec); i++) {
		bool     ack  = false;
		nni_msg  *vmsg = msg_vec[i];
		uint8_t   qos_pac;
		property *prop        = NULL;
		uint8_t   reason_code = 0;
		uint8_t   ack_cmd     = 0;
		uint16_t  packet_id   = 0;
		uint8_t   cmd         = nni_msg_cmd_type(vmsg);
		if (cmd == CMD_PUBLISH) {
			qos_pac = nni_msg_get_pub_qos(vmsg);
			if (qos_pac > 0) {
				// flow control, check rx_max
				// recv_quota as length of lmq
				if (p->ws_param->pro_ver == 5) {
					if (p->qrecv_quota > 0) {
						p->qrecv_quota--;
					} else {
						rv = NNG_ECLOSED;
						p->err_code = QUOTA_EXCEEDED;
						goto skip;
					}
				}

				if (qos_pac == 1) {
					ack_cmd = CMD_PUBACK;
				} else if (qos_pac == 2) {
					ack_cmd = CMD_PUBREC;
				} else {
					log_warn("Wrong QoS level!");
					rv = NNG_ECLOSED;
					p->err_code = PROTOCOL_ERROR;
					goto skip;
				}
				if ((packet_id = nni_msg_get_pub_pid(vmsg)) ==
				    0) {
					log_warn("0 Packet ID in QoS Message!");
					rv = NNG_ECLOSED;
					p->err_code = PROTOCOL_ERROR;
					goto skip;
				}
				ack = true;
			}
		} else if (cmd == CMD_PUBREC) {
			if ((rv = nni_mqtt_pubres_decode(vmsg, &packet_id, &reason_code, &prop,
			        p->ws_param->pro_ver)) != 0) {
				log_warn("decode PUBREC variable header failed!");
				p->err_code = PROTOCOL_ERROR;
				goto skip;
			}
			ack_cmd = CMD_PUBREL;
			ack     = true;
		} else if (cmd == CMD_PUBREL) {
			if ((rv = nni_mqtt_pubres_decode(vmsg, &packet_id, &reason_code, &prop,
			        p->ws_param->pro_ver)) != 0) {
				log_warn("decode PUBREL variable header failed!");
				p->err_code = PROTOCOL_ERROR;
				goto skip;
			}
			ack_cmd = CMD_PUBCOMP;
			ack     = true;
		} else if (cmd == CMD_PUBACK || cmd == CMD_PUBCOMP) {
			if ((rv = nni_mqtt_pubres_decode(vmsg, &packet_id, &reason_code, &prop,
			        p->ws_param->pro_ver)) != 0) {
				log_warn("decode PUBACK or PUBCOMP variable header "
				          "failed!");
				p->err_code = PROTOCOL_ERROR;
				goto skip;
			}
			// MQTT V5 flow control
			if (p->ws_param->pro_ver == 5) {
				property_free(prop);
				p->qsend_quota++;
			}
		} 

		if (ack == true) {
			// alloc a msg here costs memory. However we must do it for the
			// sake of compatibility with nng.
			nni_msg *qmsg;
			if ((rv = nni_msg_alloc(&qmsg, 0)) != 0) {
				ack = false;
				rv  = NMQ_SERVER_BUSY;
				log_error("ERROR: OOM in WebSocket");
				p->err_code = SERVER_UNAVAILABLE;
				goto reset;
			}
			// TODO set reason code or property here if necessary
			nni_msg_set_cmd_type(qmsg, ack_cmd);
			nni_mqtt_msgack_encode(qmsg, packet_id, reason_code,
			    prop, p->ws_param->pro_ver);
			nni_mqtt_pubres_header_encode(qmsg, ack_cmd);
			if (!nni_aio_busy(p->qsaio)) {
				iov[0].iov_len = nni_msg_header_len(qmsg);
				iov[0].iov_buf = nni_msg_header(qmsg);
				iov[1].iov_len = nni_msg_len(qmsg);
				iov[1].iov_buf = nni_msg_body(qmsg);
				nni_aio_set_msg(p->qsaio, qmsg);
				// send ACK down...
				nni_aio_set_iov(p->qsaio, 2, iov);
				nng_stream_send(p->ws, p->qsaio);
			} else {
				if (nni_lmq_full(&p->rslmq)) {
					// Make space for the new message.
					if (nni_lmq_cap(&p->rslmq) <= NANO_MAX_QOS_PACKET) {
						if ((rv = nni_lmq_resize(&p->rslmq,
						         nni_lmq_cap(&p->rslmq) * 2)) == 0) {
							if (nni_lmq_put(&p->rslmq, qmsg) != 0)
								nni_msg_free(qmsg);
						} else {
							log_warn("QoS Ack msg lost!");
							nni_msg_free(qmsg);
						}
					} else {
						nni_msg *old;
						(void) nni_lmq_get(&p->rslmq, &old);
						log_warn("QoS Ack msg lost!");
						nni_msg_free(old);
						if (nni_lmq_put(&p->rslmq, qmsg) != 0)
							nni_msg_free(qmsg);
					}
				} else {
					if (nni_lmq_put(&p->rslmq, qmsg) != 0)
						nni_msg_free(qmsg);
				}
			}
			ack = false;
			
		}
	}

	// Return the first msg this time
	nni_aio_set_msg(uaio, smsg);
skip:
	nni_aio_set_output(uaio, 0, p);
	nni_mtx_unlock(&p->mtx);
	nni_aio_finish(uaio, 0, 0);
	if (msg_vec)
		cvector_free(msg_vec);
	return;
reset:
	// TODO memleak in reset state ....
	p->gotrxhead  = 0;
	p->wantrxhead = 0;
	// a potential memleak case here
	nng_stream_close(p->ws);
	if (p->ep_aio != NULL) {
	// If the connection is closed during MQTT Connect, we need to pass back a fixed
	// error code to nng_listener which is NNG_ECONNABORTED. Otherwise it is confused 
	// with the accept file descriptor being closed.
	// listener will treat this errorcode as TCP err. so NNG_ECLOSED shall not be used.
		rv = NNG_ECONNABORTED;
		nni_aio_finish_error(p->ep_aio, rv);
	} else if (uaio != NULL) {
		nni_aio_set_msg(uaio, NULL);
		nni_aio_finish_error(uaio, rv);
	}
	nni_mtx_unlock(&p->mtx);
	if (smsg != NULL)
		nni_msg_free(smsg);
	if (p->tmp_msg != NULL) {
		nni_msg_free(p->tmp_msg);
		p->tmp_msg = NULL;
	}
	if (msg_vec)
		cvector_free(msg_vec);
	if (msg != NULL)
		nni_msg_free(msg);
	return;
}

static void
wstran_pipe_recv_cancel(nni_aio *aio, void *arg, int rv)
{
	ws_pipe *p = arg;
	if (p->user_rxaio != aio) {
		return;
	}
	p->user_rxaio = NULL;
	nni_aio_abort(p->rxaio, rv);
	nni_aio_finish_error(aio, rv);
}

static void
wstran_pipe_recv(void *arg, nni_aio *aio)
{
	nni_msg *msg = NULL;
	ws_pipe *p = arg;
	int      rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
	if (p->err_code != SUCCESS) {
		log_warn("Close WebSocket connection due to err %d", p->err_code);
		nni_aio_finish_error(aio, p->err_code);
		return;
	}
	nni_mtx_lock(&p->mtx);
	// Get msg from recv lmq
	if (nni_lmq_get(&p->recvlmq, &msg) == 0) {
		nni_aio_set_msg(aio, msg);
		nni_mtx_unlock(&p->mtx);
		nni_aio_finish(aio, 0, nni_msg_len(msg));
		return;
	}
	if ((rv = nni_aio_schedule(aio, wstran_pipe_recv_cancel, p)) != 0) {
		nni_mtx_unlock(&p->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	p->user_rxaio = aio;
	nng_stream_recv(p->ws, p->rxaio);
	nni_mtx_unlock(&p->mtx);
}
static void
wstran_pipe_send_cancel(nni_aio *aio, void *arg, int rv)
{
	ws_pipe *p = arg;
	if (p->user_txaio != aio) {
		return;
	}
	p->user_txaio = NULL;
	nni_aio_abort(p->txaio, rv);
	nni_aio_finish_error(aio, rv);
}

static inline void
wstran_pipe_send_start_v4(ws_pipe *p, nni_msg *msg, nni_aio *aio)
{
	nni_msg  *smsg = NULL;
	int       niov;
	nni_iov   iov[8];
	nni_pipe *pipe = p->npipe;
	uint8_t   qos = 0;


	if (nni_msg_get_type(msg) != CMD_PUBLISH)
		goto send;

	// never modify the original msg
	int           len_offset = 0;
	uint8_t *     body, *header, qos_pac, prop_bytes = 0;
	uint16_t      pid;
	uint32_t      property_len = 0;
	size_t        tlen, rlen, mlen, qlength, plength;
	bool          is_sqlite = p->conf->sqlite.enable;

	body    = nni_msg_body(msg);
	header  = nni_msg_header(msg);
	niov    = 0;
	qlength = 0;
	plength = 0;
	mlen    = nni_msg_len(msg);
	qos_pac = nni_msg_get_pub_qos(msg);
	NNI_GET16(body, tlen);

	subinfo *info, *tinfo = NULL;
	nni_msg_alloc(&smsg, 0);
	if (nni_msg_cmd_type(msg) == CMD_PUBLISH_V5) {
		// V5 to V4 shrink msg, remove property length
		// APP layer must give topic name even if topic
		// alias is set
		if (qos_pac > 0) {
			property_len =
			    get_var_integer(body + 4 + tlen, &prop_bytes);

		} else {
			property_len =
			    get_var_integer(body + 2 + tlen, &prop_bytes);
		}
		plength       = property_len + prop_bytes;
	}

	NNI_LIST_FOREACH (p->npipe->subinfol, info) {
		if (tinfo != NULL && info != tinfo ) {
			continue;
		}
		tinfo = NULL;
		len_offset=0;
		char *sub_topic = info->topic;
		if (sub_topic[0] == '$') {
			if (0 == strncmp(sub_topic, "$share/", strlen("$share/"))) {
				sub_topic = strchr(sub_topic, '/');
				sub_topic++;
				sub_topic = strchr(sub_topic, '/');
				sub_topic++;
			}
		}
		if (topic_filtern(sub_topic, (char *) (body + 2), tlen)) {
			uint8_t pos    = 1, var_extra[2], fixheader,
			        tmp[4] = { 0 };
			qos            = info->qos;
			fixheader      = *header;

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
			rlen = put_var_integer(tmp,
			    get_var_integer(header, &pos) + len_offset -
			        plength);
			*(p->qos_buf + qlength) = fixheader;
			// copy remaining length
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
			if (qos > 0) {
				// set pid
				len_offset = 2;
				nni_msg *old;
				// packetid in aio to differ resend msg
				// TODO replace it with set prov data
				pid = (uint16_t)(size_t) nni_aio_get_prov_data(aio);
				if (pid == 0) {
					// first time send this msg
					pid = nni_pipe_inc_packetid(pipe);
					// store msg for qos retrying
					nni_msg_clone(msg);
					if ((old = nni_qos_db_get(is_sqlite,
					         pipe->nano_qos_db, pipe->p_id,
					         pid)) != NULL) {
						// TODO packetid already
						// exists. we need to
						// replace old with new one
						// print warning to users
						nni_println("ERROR: packet id "
						            "duplicates in "
						            "nano_qos_db");
						nni_qos_db_remove_msg(
						    is_sqlite,
						    pipe->nano_qos_db, old);
					}
					old = msg;
					nni_qos_db_set(is_sqlite,
					    pipe->nano_qos_db, pipe->p_id, pid,
					    old);
					nni_qos_db_remove_oldest(is_sqlite,
					    pipe->nano_qos_db,
					    p->conf->sqlite.disk_cache_size);
				}
				NNI_PUT16(var_extra, pid);
				// copy packet id
				memcpy(p->qos_buf + qlength, var_extra, 2);
			} else if (qos_pac > 0) {
				//ignore the packet id of original packet
				len_offset += 2;
			}
			// 2nd part of variable header: pid
			iov[niov].iov_buf = p->qos_buf + qlength;
			iov[niov].iov_len = qos > 0 ? 2 : 0;
			niov++;
			qlength += qos > 0 ? 2 : 0;
			// body
			iov[niov].iov_buf = body + 2 + tlen + len_offset + plength;
			iov[niov].iov_len = mlen - 2 - len_offset - tlen - plength;
			niov++;
			// apending directly
			for (int i = 0; i < niov; i++) {
				nni_msg_append(
				    smsg, iov[i].iov_buf, iov[i].iov_len);
			}
			niov = 0;
		}
	}

	// duplicated msg is gonna be freed by http. so we free old one
	// here
	nni_msg_free(msg);
	msg = smsg;

// normal sending if it is not PUBLISH
send:
	nni_aio_set_msg(aio, msg);
	nni_aio_set_msg(p->txaio, msg);
	nni_aio_set_msg(aio, NULL);
	// verify connect
	// for websocket, cmd type is 0x00 for PUBLISH
	if (nni_msg_cmd_type(msg) == CMD_CONNACK) {
		uint8_t *header = nni_msg_header(msg);
		if (*(header + 3) != 0x00) {
			p->closed = true;
			// TODO get err code from CONNACK
			p->err_code = NOT_AUTHORIZED;
		}
	}
	nng_stream_send(p->ws, p->txaio);
}



static inline void
wstran_pipe_send_start_v5(ws_pipe *p, nni_msg *msg, nni_aio *aio)
{
	nni_msg  *smsg = NULL;
	int       niov;
	nni_iov   iov[8];
	nni_pipe *pipe = p->npipe;
	uint8_t   qos = 0;


	if (nni_msg_get_type(msg) != CMD_PUBLISH)
		goto send;

	// never modify the original msg
	uint8_t *     body, *header, qos_pac, tprop_bytes = 0, prop_bytes = 0;
	target_prover target_prover;
	int           len_offset = 0, sub_id = 0;
	uint16_t      pid;
	uint32_t      id_bytes = 0, property_len = 0;
	size_t        tlen, rlen, mlen, hlen, qlength, plength;
	bool          is_sqlite = p->conf->sqlite.enable;

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
	if (total_len > p->ws_param->max_packet_size) {
		// drop msg and finish aio
		// pretend it has been sent
		log_warn("msg dropped due to exceed max packet size %ld %ld!",
				total_len, p->ws_param->max_packet_size);
		nni_msg_free(msg);
		nni_aio_set_msg(aio, NULL);
		nni_aio_list_remove(aio);
		nni_aio_finish(aio, 0, 0);
		return;
	}

	if (nni_msg_cmd_type(msg) == CMD_PUBLISH_V5) {
		// V5 to V4 shrink msg, remove property length
		// APP layer must give topic name even if topic
		// alias is set
		if (qos_pac > 0) {
			property_len = get_var_integer(
			    body + 4 + tlen, &prop_bytes);

		} else {
			property_len = get_var_integer(
			    body + 2 + tlen, &prop_bytes);
		}
		target_prover = MQTTV5;
		plength = property_len + prop_bytes;
	}

	// subid
	subinfo *info = NULL;
	nni_msg_alloc(&smsg, 0);

	NNI_LIST_FOREACH (p->npipe->subinfol, info) {
		if (info->no_local == 1 &&
		    p->npipe->p_id == nni_msg_get_pipe(msg)) {
			continue;
		}
		len_offset      = 0;
		char *sub_topic = info->topic;
		if (sub_topic[0] == '$') {
			if (0 ==
			    strncmp(sub_topic, "$share/", strlen("$share/"))) {
				sub_topic = strchr(sub_topic, '/');
				sub_topic++;
				sub_topic = strchr(sub_topic, '/');
				sub_topic++;
			}
		}
		if (topic_filtern(sub_topic, (char *) (body + 2), tlen)) {
			uint8_t  var_extra[2], fixheader, tmp[4] = { 0 }, pos = 1;
			uint8_t  proplen[4] = { 0 }, var_subid[5] = { 0 };
			sub_id       = info->subid;
			qos          = info->qos;

			//else use original var payload & pid
			fixheader = *header;
			if (nni_msg_cmd_type(msg) == CMD_PUBLISH) {
				// V4 to V5 add 0 property length
				target_prover = MQTTV4_V5;
				prop_bytes    = 1;
				tprop_bytes   = 1;
				len_offset    = 1;
			}
			if (info->rap == 0 && !nni_mqtt_msg_get_sub_retain_bool(msg)) {
				fixheader = fixheader & 0xFE;
			}
			if (sub_id != 0) {
				var_subid[0] = 0x0B;
				id_bytes = put_var_integer(var_subid+1, sub_id);
				tprop_bytes = put_var_integer(proplen, property_len+1+id_bytes);
				len_offset += (tprop_bytes - prop_bytes + 1 + id_bytes);
			}

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
				pid = (uint16_t)(size_t) nni_aio_get_prov_data(
				    aio);
				if (pid == 0) {
					// first time send this msg
					pid = nni_pipe_inc_packetid(pipe);
					// store msg for qos retrying
					nni_msg_clone(msg);
					if ((old = nni_qos_db_get(is_sqlite,
					         pipe->nano_qos_db, pipe->p_id,
					         pid)) != NULL) {
						// TODO packetid already
						// exists. we need to
						// replace old with new one
						// print warning to users
						nni_println("ERROR: packet id "
						            "duplicates in "
						            "nano_qos_db");
						nni_qos_db_remove_msg(
						    is_sqlite,
						    pipe->nano_qos_db, old);
					}
					old = msg;
					nni_qos_db_set(is_sqlite,
					    pipe->nano_qos_db, pipe->p_id, pid,
					    old);
					nni_qos_db_remove_oldest(is_sqlite,
					    pipe->nano_qos_db,
					    p->conf->sqlite.disk_cache_size);
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
			// apending directly
			for (int i = 0; i < niov; i++) {
				nni_msg_append(
				    smsg, iov[i].iov_buf, iov[i].iov_len);
			}
			niov = 0;
		}
	}

	// duplicated msg is gonna be freed by http. so we free old one
	// here
	nni_msg_free(msg);
	msg = smsg;

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
			// nni_aio_set_prov_data(txaio, NULL);
			nni_aio_set_msg(aio, NULL);
			nni_aio_finish(aio, 0, 0);
			return;
		}
	}

// normal sending if it is not PUBLISH
send:
	nni_aio_set_msg(aio, msg);
	nni_aio_set_msg(p->txaio, msg);
	nni_aio_set_msg(aio, NULL);
	// verify connect
	if (nni_msg_cmd_type(msg) == CMD_CONNACK) {
		uint8_t *header = nni_msg_header(msg);
		if (*(header + 3) != 0x00) {
			p->closed = true;
			p->err_code = NOT_AUTHORIZED;
		}
	}
	nng_stream_send(p->ws, p->txaio);
}

static void
wstran_pipe_send_start(ws_pipe *p)
{
	nni_msg *msg;
	nng_aio *aio = p->user_txaio;
	msg          = nni_aio_get_msg(aio);

	if (msg == NULL || p->ws_param == NULL) {
		// TODO error handler
		log_error("ERROR: sending NULL msg or pipe is invalid!");
		nni_aio_finish_error(aio, NNG_ECANCELED);
		return;
	}
	if (p->closed) {
		log_error("ERROR: sending NULL msg or pipe is closed!");
		nni_aio_finish_error(aio, NNG_ECANCELED);
		nni_msg_free(msg);
		return;
	}
	if (p->ws_param->pro_ver == 4) {
		wstran_pipe_send_start_v4(p, msg, aio);
		return;
	} else if (p->ws_param->pro_ver == 5) {
		wstran_pipe_send_start_v5(p, msg, aio);
		return;
	}

}

static void
wstran_pipe_send(void *arg, nni_aio *aio)
{
	ws_pipe *p = arg;
	int      rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&p->mtx);
	if ((rv = nni_aio_schedule(aio, wstran_pipe_send_cancel, p)) != 0) {
		nni_mtx_unlock(&p->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	p->user_txaio = aio;
	wstran_pipe_send_start(p);

	nni_mtx_unlock(&p->mtx);
}

static void
wstran_pipe_stop(void *arg)
{
	ws_pipe *p = arg;

	nni_aio_stop(p->rxaio);
	nni_aio_stop(p->txaio);
	nni_aio_stop(p->qsaio);
}

static int
wstran_pipe_init(void *arg, nni_pipe *pipe)
{
	int      rv, id;
	NNI_ARG_UNUSED(rv);
	NNI_ARG_UNUSED(id);
	ws_pipe *p            = arg;
	// sth wrong with negotiation, close it
	log_trace(" ************ wstran_pipe_init [%p] ************ ", arg);
	nni_pipe_set_conn_param(pipe, p->ws_param);
	p->npipe      = pipe;

	p->gotrxhead  = 0;
	p->wantrxhead = 0;
	p->ep_aio     = NULL;
	if (p->closed)
		return (0);
	char    *cid;
	uint32_t clientid_key = 0;
	cid = (char *) conn_param_get_clientid(p->ws_param);
	clientid_key = DJBHashn(cid, strlen(cid));
	id = nni_pipe_id(pipe);
	rv = nni_pipe_set_pid(pipe, clientid_key);
	log_debug("change p_id by hashing from %d to %d rv %d",
			   id, clientid_key, rv);

	p->qos_buf = nng_zalloc(16 + NNI_NANO_MAX_PACKET_SIZE);
	nni_lmq_init(&p->recvlmq, 1024);
	nni_lmq_init(&p->rslmq, 1024);
	// the size limit of qos_buf reserve 1 byte for property length
	p->qlength = 16 + NNI_NANO_MAX_PACKET_SIZE;
	if (!p->conf->sqlite.enable && pipe->nano_qos_db == NULL) {
		nni_qos_db_init_id_hash(pipe->nano_qos_db);
	}

	return (0);
}

static void
wstran_pipe_fini(void *arg)
{
	ws_pipe *p = arg;
	nni_mtx_lock(&p->mtx);
	nng_stream_free(p->ws);
	nni_aio_free(p->rxaio);
	nni_aio_free(p->txaio);
	// We have to free msg here for a failed send
	// due to the messy design of NNG WebSocket
	nni_aio_wait(p->qsaio);
	nni_aio_free(p->qsaio);
	nni_msg_free(p->tmp_msg);
	nni_mtx_unlock(&p->mtx);
	nni_mtx_fini(&p->mtx);
	nng_free(p->qos_buf, 16 + NNI_NANO_MAX_PACKET_SIZE);
	nni_lmq_flush(&p->recvlmq);
	nni_lmq_fini(&p->recvlmq);
	nni_lmq_flush(&p->rslmq);
	nni_lmq_fini(&p->rslmq);
	NNI_FREE_STRUCT(p);
}

static void
wstran_pipe_close(void *arg)
{
	ws_pipe *p = arg;
	nni_mtx_lock(&p->mtx);
	nni_aio_close(p->rxaio);
	nni_aio_abort(p->qsaio, NNG_ECANCELED);
	nni_aio_close(p->qsaio);
	nni_aio_close(p->txaio);
	nng_stream_close(p->ws);
	nni_mtx_unlock(&p->mtx);
}

static int
wstran_pipe_alloc(ws_pipe **pipep, void *ws)
{
	ws_pipe *p;
	int      rv;

	if ((p = NNI_ALLOC_STRUCT(p)) == NULL) {
		return (NNG_ENOMEM);
	}
	nni_mtx_init(&p->mtx);

	// Initialize AIOs.
	if (((rv = nni_aio_alloc(&p->txaio, wstran_pipe_send_cb, p)) != 0) ||
	    ((rv = nni_aio_alloc(&p->qsaio, wstran_pipe_qos_send_cb, p)) != 0) ||
	    ((rv = nni_aio_alloc(&p->rxaio, wstran_pipe_recv_cb, p)) != 0)) {
		wstran_pipe_fini(p);
		return (rv);
	}
	p->ws = ws;

	*pipep = p;
	return (0);
}

static uint16_t
wstran_pipe_peer(void *arg)
{
	ws_pipe *p = arg;

	return (p->peer);
}

static int
ws_listener_bind(void *arg)
{
	ws_listener *l = arg;
	int          rv;

	if ((rv = nng_stream_listener_listen(l->listener)) == 0) {
		l->started = true;
	}
	return (rv);
}

static void
ws_listener_cancel(nni_aio *aio, void *arg, int rv)
{
	ws_listener *l = arg;

	nni_mtx_lock(&l->mtx);
	if (nni_aio_list_active(aio)) {
		nni_aio_list_remove(aio);
		nni_aio_finish_error(aio, rv);
	}
	nni_mtx_unlock(&l->mtx);
}

static void
wstran_listener_accept(void *arg, nni_aio *aio)
{
	ws_listener *l = arg;
	int          rv;

	// We already bound, so we just need to look for an available
	// pipe (created by the handler), and match it.
	// Otherwise we stick the AIO in the accept list.
	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&l->mtx);
	if ((rv = nni_aio_schedule(aio, ws_listener_cancel, l)) != 0) {
		nni_mtx_unlock(&l->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_list_append(&l->aios, aio);
	if (aio == nni_list_first(&l->aios)) {
		nng_stream_listener_accept(l->listener, l->accaio);
	}
	nni_mtx_unlock(&l->mtx);
}

static const nni_option ws_pipe_options[] = {
	// terminate list
	{
	    .o_name = NULL,
	}
};

static int
wstran_pipe_getopt(
    void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	ws_pipe *p = arg;
	int      rv;

	if ((rv = nni_stream_get(p->ws, name, buf, szp, t)) == NNG_ENOTSUP) {
		rv = nni_getopt(ws_pipe_options, name, p, buf, szp, t);
	}
	return (rv);
}

static nni_sp_pipe_ops ws_pipe_ops = {
	.p_init   = wstran_pipe_init,
	.p_fini   = wstran_pipe_fini,
	.p_stop   = wstran_pipe_stop,
	.p_send   = wstran_pipe_send,
	.p_recv   = wstran_pipe_recv,
	.p_close  = wstran_pipe_close,
	.p_peer   = wstran_pipe_peer,
	.p_getopt = wstran_pipe_getopt,
};

static void
wstran_listener_fini(void *arg)
{
	ws_listener *l = arg;

	nni_aio_stop(l->accaio);
	nng_stream_listener_free(l->listener);
	nni_aio_free(l->accaio);
	nni_mtx_fini(&l->mtx);
	NNI_FREE_STRUCT(l);
}

static void
wstran_listener_close(void *arg)
{
	ws_listener *l = arg;

	nni_aio_close(l->accaio);
	nng_stream_listener_close(l->listener);
}

static void
ws_pipe_start(ws_pipe *pipe, nng_stream *conn, ws_listener *l)
{
	NNI_ARG_UNUSED(conn);
	ws_pipe *p = pipe;
	log_trace("ws_pipe_start!");
	p->qrecv_quota = NANO_MAX_QOS_PACKET;
	p->conf        = l->conf;
	p->closed      = false;
	nng_stream_recv(p->ws, p->rxaio);
}

static void
wstran_accept_cb(void *arg)
{
	ws_listener *l    = arg;
	nni_aio *    aaio = l->accaio;
	nni_aio *    uaio;
	int          rv;

	nni_mtx_lock(&l->mtx);
	uaio = nni_list_first(&l->aios);
	if ((rv = nni_aio_result(aaio)) != 0) {
		if (uaio != NULL) {
			nni_aio_list_remove(uaio);
			nni_aio_finish_error(uaio, rv);
		}
	} else {
		nng_stream *ws = nni_aio_get_output(aaio, 0);
		if (uaio != NULL) {
			ws_pipe *p;
			// Make a pipe
			nni_aio_list_remove(uaio);
			if ((rv = wstran_pipe_alloc(&p, ws)) != 0) {
				nng_stream_close(ws);
				nni_aio_finish_error(uaio, rv);
			} else {
				p->peer = l->peer;
				p->ep_aio = uaio;
				ws_pipe_start(p, p->ws, l);
			}
		}
	}

	if (!nni_list_empty(&l->aios)) {
		nng_stream_listener_accept(l->listener, aaio);
	}
	nni_mtx_unlock(&l->mtx);
}


// TODO proto name modify
static int
wstran_listener_init(void **lp, nng_url *url, nni_listener *listener)
{
	ws_listener *l;
	int          rv;
	nni_sock *   s = nni_listener_sock(listener);
	char         name[64];

	if ((l = NNI_ALLOC_STRUCT(l)) == NULL) {
		return (NNG_ENOMEM);
	}
	nni_mtx_init(&l->mtx);

	nni_aio_list_init(&l->aios);

	l->peer = nni_sock_peer_id(s);

	snprintf(name, sizeof(name), "mqtt");

	if (((rv = nni_ws_listener_alloc(&l->listener, url)) != 0) ||
	    ((rv = nni_aio_alloc(&l->accaio, wstran_accept_cb, l)) != 0) ||
	    ((rv = nng_stream_listener_set_bool(
	          l->listener, NNI_OPT_WS_MSGMODE, true)) != 0) ||
	    ((rv = nng_stream_listener_set_string(
	          l->listener, NNG_OPT_WS_PROTOCOL, name)) != 0)) {
		wstran_listener_fini(l);
		return (rv);
	}
	*lp = l;
	return (0);
}

static void
wstran_init(void)
{
}

static void
wstran_fini(void)
{
}

static int
wstran_ep_set_conf(void *arg, const void *v, size_t sz, nni_type t)
{
	ws_listener *l = arg;
	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);
	nni_mtx_lock(&l->mtx);
	l->conf = (conf *) v;
	nni_mtx_unlock(&l->mtx);
	return 0;
}

static const nni_option wstran_ep_opts[] = {
	{
	    .o_name = NANO_CONF,
	    .o_set  = wstran_ep_set_conf,
	},
	// terminate list
	{
	    .o_name = NULL,
	},
};


static int
wstran_listener_get(
    void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	ws_listener *l = arg;
	int          rv;

	rv = nni_stream_listener_get(l->listener, name, buf, szp, t);
	if (rv == NNG_ENOTSUP) {
		rv = nni_getopt(wstran_ep_opts, name, l, buf, szp, t);
	}
	return (rv);
}

static int
wstran_listener_set(
    void *arg, const char *name, const void *buf, size_t sz, nni_type t)
{
	ws_listener *l = arg;
	int          rv;

	rv = nni_stream_listener_set(l->listener, name, buf, sz, t);
	if (rv == NNG_ENOTSUP) {
		rv = nni_setopt(wstran_ep_opts, name, l, buf, sz, t);
	}
	return (rv);
}

static nni_sp_listener_ops ws_listener_ops = {
	.l_init   = wstran_listener_init,
	.l_fini   = wstran_listener_fini,
	.l_bind   = ws_listener_bind,
	.l_accept = wstran_listener_accept,
	.l_close  = wstran_listener_close,
	.l_setopt = wstran_listener_set,
	.l_getopt = wstran_listener_get,
};

static nni_sp_tran ws__tran = {
	.tran_scheme   = "nmq+ws",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran ws4__tran = {
	.tran_scheme   = "nmq+ws4",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran ws6__tran = {
	.tran_scheme   = "nmq+ws6",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran ws_tran = {
	.tran_scheme   = "nmq-ws",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran ws4_tran = {
	.tran_scheme   = "nmq-ws4",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran ws6_tran = {
	.tran_scheme   = "nmq-ws6",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

#ifndef NNG_ELIDE_DEPRECATED
int
nng_nmq_ws_register(void)
{
	return (nni_init());
}

int
nng_nmq_wss_register(void)
{
	return (nni_init());
}
#endif

void
nni_nmq_ws_register(void)
{
	nni_sp_tran_register(&ws_tran);
	nni_sp_tran_register(&ws4_tran);
	nni_sp_tran_register(&ws6_tran);
	nni_sp_tran_register(&ws__tran);
	nni_sp_tran_register(&ws4__tran);
	nni_sp_tran_register(&ws6__tran);
}

#ifdef NNG_TRANSPORT_WSS

static nni_sp_tran wss__tran = {
	.tran_scheme   = "nmq+wss",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran wss4__tran = {
	.tran_scheme   = "nmq+wss4",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran wss6__tran = {
	.tran_scheme   = "nmq+wss6",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran wss_tran = {
	.tran_scheme   = "nmq-wss",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran wss4_tran = {
	.tran_scheme   = "nmq-wss4",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

static nni_sp_tran wss6_tran = {
	.tran_scheme   = "nmq-wss6",
	.tran_dialer   = NULL,
	.tran_listener = &ws_listener_ops,
	.tran_pipe     = &ws_pipe_ops,
	.tran_init     = wstran_init,
	.tran_fini     = wstran_fini,
};

void
nni_nmq_wss_register(void)
{
	nni_sp_tran_register(&wss_tran);
	nni_sp_tran_register(&wss4_tran);
	nni_sp_tran_register(&wss6_tran);
	nni_sp_tran_register(&wss__tran);
	nni_sp_tran_register(&wss4__tran);
	nni_sp_tran_register(&wss6__tran);
}

#endif // NNG_TRANSPORT_WSS
