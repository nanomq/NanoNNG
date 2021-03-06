//
// Copyright 2020 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "core/nng_impl.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/supplemental/sqlite/sqlite3.h"
#include "supplemental/mqtt/mqtt_msg.h"
#include "supplemental/mqtt/mqtt_qos_db_api.h"

// MQTT client implementation.
//
// 1. MQTT client sockets have a single implicit dialer, and cannot
//    support creation of additional dialers or listeners.
// 2. Send sends PUBLISH messages.
// 3. Receive is used to receive published data from the server.

#define NNG_MQTT_SELF 0
#define NNG_MQTT_SELF_NAME "mqtt-client"
#define NNG_MQTT_PEER 0
#define NNG_MQTT_PEER_NAME "mqtt-server"

#define DB_NAME "mqtt_qos_db.db"

typedef struct mqtt_sock_s mqtt_sock_t;
typedef struct mqtt_pipe_s mqtt_pipe_t;
typedef struct mqtt_ctx_s  mqtt_ctx_t;

static void mqtt_sock_init(void *arg, nni_sock *sock);
static void mqtt_sock_fini(void *arg);
static void mqtt_sock_open(void *arg);
static void mqtt_sock_send(void *arg, nni_aio *aio);
static void mqtt_sock_recv(void *arg, nni_aio *aio);
static int  mqtt_sock_set_conf_with_db(
     void *arg, const void *v, size_t sz, nni_opt_type t);
static void mqtt_send_cb(void *arg);
static void mqtt_recv_cb(void *arg);
static void mqtt_timer_cb(void *arg);

static int  mqtt_pipe_init(void *arg, nni_pipe *pipe, void *s);
static void mqtt_pipe_fini(void *arg);
static int  mqtt_pipe_start(void *arg);
static void mqtt_pipe_stop(void *arg);
static void mqtt_pipe_close(void *arg);

static void mqtt_ctx_init(void *arg, void *sock);
static void mqtt_ctx_fini(void *arg);
static void mqtt_ctx_send(void *arg, nni_aio *aio);
static void mqtt_ctx_recv(void *arg, nni_aio *aio);

static bool  get_persist(mqtt_sock_t *s);
static char *get_config_name(mqtt_sock_t *s);
static void  flush_offline_cache(mqtt_sock_t *s);
static nni_msg* get_cache_msg(mqtt_sock_t *s);

typedef nni_mqtt_packet_type packet_type_t;

// A mqtt_ctx_s is our per-ctx protocol private state.
struct mqtt_ctx_s {
	mqtt_sock_t *mqtt_sock;
	nni_aio *  saio;             // send aio
	nni_aio *  raio;             // recv aio
	nni_list_node sqnode;
	nni_list_node rqnode;
};

// A mqtt_pipe_s is our per-pipe protocol private structure.
struct mqtt_pipe_s {
	nni_atomic_bool closed;
	nni_atomic_int  next_packet_id; // next packet id to use
	nni_pipe *      pipe;
	mqtt_sock_t *   mqtt_sock;

	void *sent_unack; // send messages unacknowledged

	nni_id_map      recv_unack;    // recv messages unacknowledged
	nni_aio         send_aio;      // send aio to the underlying transport
	nni_aio         recv_aio;      // recv aio to the underlying transport
	nni_aio         time_aio;      // timer aio to resend unack msg
	nni_lmq         recv_messages; // recv messages queue
	nni_lmq         send_messages; // send messages queue
	nni_lmq         ctx_aios;      // awaiting aio of QoS
	bool            busy;
};

// A mqtt_sock_s is our per-socket protocol private structure.
struct mqtt_sock_s {
	nni_atomic_bool closed;
	nni_atomic_int  ttl;
	nni_duration    retry;
	nni_mtx         mtx;    // more fine grained mutual exclusion
	mqtt_ctx_t      master; // to which we delegate send/recv calls
	mqtt_pipe_t    *mqtt_pipe;
	nni_list        recv_queue; // ctx pending to receive
	nni_list        send_queue; // ctx pending to send (only offline msg)
	reason_code     disconnect_code; // disconnect reason code
	property       *dis_prop;        // disconnect property
#ifdef NNG_SUPP_SQLITE
	sqlite3 *sqlite_db;
	nni_lmq  offline_cache;
#endif
#ifdef NNG_HAVE_MQTT_BROKER
	conf_bridge_node *bridge_conf;
#endif
};

/******************************************************************************
 *                              Sock Implementation                           *
 ******************************************************************************/

static void
mqtt_sock_init(void *arg, nni_sock *sock)
{
	NNI_ARG_UNUSED(sock);
	mqtt_sock_t *s = arg;

	nni_atomic_init_bool(&s->closed);
	nni_atomic_set_bool(&s->closed, false);

	nni_atomic_init(&s->ttl);
	nni_atomic_set(&s->ttl, 8);

	// this is "semi random" start for request IDs.
	s->retry = NNI_SECOND * 60;

	nni_mtx_init(&s->mtx);
	mqtt_ctx_init(&s->master, s);

	s->mqtt_pipe = NULL;
	NNI_LIST_INIT(&s->recv_queue, mqtt_ctx_t, rqnode);
	NNI_LIST_INIT(&s->send_queue, mqtt_ctx_t, sqnode);
}

static void
mqtt_sock_fini(void *arg)
{
	mqtt_sock_t *s = arg;
#if defined(NNG_SUPP_SQLITE) && defined(NNG_HAVE_MQTT_BROKER)
	bool is_sqlite = get_persist(s);
	if (is_sqlite) {
		nni_qos_db_fini_sqlite(s->sqlite_db);
		nni_lmq_fini(&s->offline_cache);
	}
#endif
	mqtt_ctx_fini(&s->master);
	nni_mtx_fini(&s->mtx);
}

static void
mqtt_sock_open(void *arg)
{
	NNI_ARG_UNUSED(arg);
}

static int
mqtt_sock_get_disconnect_prop(void *arg, void *v, size_t *szp, nni_opt_type t)
{
	mqtt_sock_t *s = arg;
	int              rv;

	nni_mtx_lock(&s->mtx);
	rv = nni_copyout_ptr(s->dis_prop, v, szp, t);
	nni_mtx_lock(&s->mtx);
	return (rv);
}

static int
mqtt_sock_get_disconnect_code(void *arg, void *v, size_t *sz, nni_opt_type t)
{
	NNI_ARG_UNUSED(sz);
	mqtt_sock_t *s = arg;
	int              rv;

	nni_mtx_lock(&s->mtx);
	rv = nni_copyin_int(v, &s->disconnect_code, sizeof(reason_code), 0, 256, t);
	nni_mtx_unlock(&s->mtx);
	return (rv);
}

static int
mqtt_sock_set_conf_with_db(void *arg, const void *v, size_t sz, nni_opt_type t)
{
	NNI_ARG_UNUSED(sz);
#ifdef NNG_HAVE_MQTT_BROKER
	mqtt_sock_t *s = arg;
	if (t == NNI_TYPE_OPAQUE) {
		nni_mtx_lock(&s->mtx);
		s->bridge_conf = (conf_bridge_node *) v;

#ifdef NNG_SUPP_SQLITE
		conf_bridge_node *bridge_conf = s->bridge_conf;
		if (bridge_conf != NULL && bridge_conf->sqlite->enable) {
			s->retry = bridge_conf->sqlite->resend_interval;
			nni_lmq_init(&s->offline_cache,
			    bridge_conf->sqlite->flush_mem_threshold);
			nni_qos_db_init_sqlite(s->sqlite_db,
			    bridge_conf->sqlite->mounted_file_path, DB_NAME,
			    false);
			nni_qos_db_reset_client_msg_pipe_id(
			    bridge_conf->sqlite->enable, s->sqlite_db,
			    bridge_conf->name);
			nni_mqtt_qos_db_remove_all_client_offline_msg(
			    s->sqlite_db, bridge_conf->name);
			nni_mqtt_qos_db_set_client_info(s->sqlite_db,
			    bridge_conf->name, NULL, "MQTT",
			    bridge_conf->proto_ver);
		}
#endif
		nni_mtx_unlock(&s->mtx);
		return 0;
	}
#else
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(v);
	NNI_ARG_UNUSED(t);
#endif
	return NNG_EUNREACHABLE;
}

static void
mqtt_sock_close(void *arg)
{
	mqtt_sock_t *s = arg;
	mqtt_ctx_t  *ctx;
	nni_aio *aio;
	nni_msg *msg;

	nni_atomic_set_bool(&s->closed, true);
	//clean ctx queue when pipe was closed.
	while ((ctx = nni_list_first(&s->send_queue)) != NULL) {
		// Pipe was closed.  just push an error back to the
		// entire socket, because we only have one pipe
		nni_list_remove(&s->send_queue, ctx);
		aio       = ctx->saio;
		ctx->saio = NULL;
		msg       = nni_aio_get_msg(aio);
		nni_aio_set_msg(aio, NULL);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		nni_msg_free(msg);
	}
	while ((ctx = nni_list_first(&s->recv_queue)) != NULL) {
		// Pipe was closed.  just push an error back to the
		// entire socket, because we only have one pipe
		nni_list_remove(&s->recv_queue, ctx);
		aio       = ctx->raio;
		ctx->raio = NULL;
		// there should be no msg waiting
		nni_aio_finish_error(aio, NNG_ECLOSED);
	}
}

static void
mqtt_sock_send(void *arg, nni_aio *aio)
{
	mqtt_sock_t *s = arg;
	mqtt_ctx_send(&s->master, aio);
}

static void
mqtt_sock_recv(void *arg, nni_aio *aio)
{
	mqtt_sock_t *s = arg;
	mqtt_ctx_recv(&s->master, aio);
}

/******************************************************************************
 *                              Pipe Implementation                           *
 ******************************************************************************/

static uint16_t
mqtt_pipe_get_next_packet_id(mqtt_pipe_t *p)
{
	int packet_id;
	do {
		packet_id = nni_atomic_get(&p->next_packet_id);
	} while (
	    !nni_atomic_cas(&p->next_packet_id, packet_id, packet_id + 1));
	return packet_id & 0xFFFF;
}

static int
mqtt_pipe_init(void *arg, nni_pipe *pipe, void *s)
{
	mqtt_pipe_t *p    = arg;

	nni_atomic_init_bool(&p->closed);
	nni_atomic_set_bool(&p->closed, false);
	nni_atomic_set(&p->next_packet_id, 1);
	p->pipe      = pipe;
	p->mqtt_sock = s;
	nni_aio_init(&p->send_aio, mqtt_send_cb, p);
	nni_aio_init(&p->recv_aio, mqtt_recv_cb, p);
	nni_aio_init(&p->time_aio, mqtt_timer_cb, p);
	// Packet IDs are 16 bits
	// We start at a random point, to minimize likelihood of
	// accidental collision across restarts.
	nni_qos_db_init_id_hash_with_opt(
	    p->sent_unack, 0x0000u, 0xffffu, true);
	nni_id_map_init(&p->recv_unack, 0x0000u, 0xffffu, true);
	nni_lmq_init(&p->recv_messages, NNG_MAX_RECV_LMQ);
	nni_lmq_init(&p->send_messages, NNG_MAX_SEND_LMQ);

	return (0);
}

static void
mqtt_pipe_fini(void *arg)
{
	mqtt_pipe_t *p = arg;
	nni_msg * msg;
	if ((msg = nni_aio_get_msg(&p->recv_aio)) != NULL) {
		nni_aio_set_msg(&p->recv_aio, NULL);
		nni_msg_free(msg);
	}
	if ((msg = nni_aio_get_msg(&p->send_aio)) != NULL) {
		nni_aio_set_msg(&p->send_aio, NULL);
		nni_msg_free(msg);
	}

	nni_aio_fini(&p->send_aio);
	nni_aio_fini(&p->recv_aio);
	nni_aio_fini(&p->time_aio);

	nni_qos_db_fini_id_hash(p->sent_unack);

	nni_id_map_fini(&p->recv_unack);
	nni_lmq_fini(&p->recv_messages);
	nni_lmq_fini(&p->send_messages);
}

static inline nni_msg *
get_cache_msg(mqtt_sock_t *s)
{
	nni_msg *msg = NULL;
#if defined(NNG_HAVE_MQTT_BROKER)
	if (s->bridge_conf == NULL) {
		return NULL;
	}
	conf_sqlite *sqlite = s->bridge_conf->sqlite;
#if defined(NNG_SUPP_SQLITE)
	if (sqlite->enable) {
		int64_t row_id = 0;

		msg = nni_mqtt_qos_db_get_client_offline_msg(
		    s->sqlite_db, &row_id, get_config_name(s));
		if (!nni_lmq_empty(&s->offline_cache)) {
			flush_offline_cache(s);
		}
		if (msg != NULL) {
			nni_mqtt_qos_db_remove_client_offline_msg(
			    s->sqlite_db, row_id);
		}
	}
#else
	NNI_ARG_UNUSED(sqlite);
	return NULL;
#endif
#else
	return NULL;
#endif
	return msg;
}

// Should be called with mutex lock hold. and it will unlock mtx.
// flag indicates if need to skip msg in sqlite 1: check sqlite 0: only aio
static inline void
mqtt_send_msg(nni_aio *aio, mqtt_ctx_t *arg)
{
	mqtt_ctx_t *     ctx   = arg;
	mqtt_sock_t *    s     = ctx->mqtt_sock;
	mqtt_pipe_t *    p     = s->mqtt_pipe;
	uint16_t         ptype = 0, packet_id = 0;
	uint8_t          qos = 0;
	nni_msg *        msg;
	nni_msg *        tmsg;

	if (NULL == aio || NULL == (msg = nni_aio_get_msg(aio))) {
		msg = get_cache_msg(s);
		if (msg == NULL) {
			goto out;
		}
	}

	ptype = nni_mqtt_msg_get_packet_type(msg);
	switch (ptype) {
	case NNG_MQTT_CONNECT:
	case NNG_MQTT_PINGREQ:
		break;

	case NNG_MQTT_PUBLISH:
		qos = nni_mqtt_msg_get_publish_qos(msg);
		if (0 == qos) {
			break; // QoS 0 need no packet id
		}
		// FALLTHROUGH
	case NNG_MQTT_SUBSCRIBE:
	case NNG_MQTT_UNSUBSCRIBE:
		packet_id     = mqtt_pipe_get_next_packet_id(p);
		nni_mqtt_msg_set_packet_id(msg, packet_id);
		nni_mqtt_msg_set_aio(msg, aio);
		tmsg = nni_id_get(p->sent_unack, packet_id);
		if (tmsg != NULL) {
			nni_plat_printf("Warning : msg %d lost due to "
			                "packetID duplicated!",
			    packet_id);
			nni_aio *m_aio = nni_mqtt_msg_get_aio(tmsg);
			if (m_aio) {
				nni_aio_finish_error(m_aio, NNG_EPROTO);
			}
			nni_msg_free(tmsg);
			nni_id_remove(p->sent_unack, packet_id);
		}
		nni_msg_clone(msg);
		if (0 != nni_id_set(p->sent_unack, packet_id, msg)) {
			nni_msg_free(msg);
		}
		break;

	default:
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish_error(aio, NNG_EPROTO);
		return;
	}
	if (!p->busy) {
		p->busy = true;
		nni_mqtt_msg_encode(msg);
		nni_aio_set_msg(&p->send_aio, msg);
		nni_aio_bump_count(
		    aio, nni_msg_header_len(msg) + nni_msg_len(msg));
		nni_pipe_send(p->pipe, &p->send_aio);
		nni_mtx_unlock(&s->mtx);
		if (0 == qos && ptype != NNG_MQTT_SUBSCRIBE &&
		    ptype != NNG_MQTT_UNSUBSCRIBE) {
			nni_aio_finish(aio, 0, 0);
		}
		return;
	}
	if (nni_lmq_full(&p->send_messages)) {
		(void) nni_lmq_get(&p->send_messages, &tmsg);
		nni_msg_free(tmsg);
	}
	if (0 != nni_lmq_put(&p->send_messages, msg)) {
		nni_println("Warning! msg lost due to busy socket");
	}
out:
	nni_mtx_unlock(&s->mtx);
	if (0 == qos && ptype != NNG_MQTT_SUBSCRIBE &&
	    ptype != NNG_MQTT_UNSUBSCRIBE) {
		nni_aio_finish(aio, 0, 0);
	}
	return;
}

static int
mqtt_pipe_start(void *arg)
{
	mqtt_pipe_t *p = arg;
	mqtt_sock_t *s = p->mqtt_sock;
	mqtt_ctx_t  *c = NULL;

	nni_mtx_lock(&s->mtx);
	s->mqtt_pipe       = p;
	s->disconnect_code = 0;
	s->dis_prop        = NULL;
	if ((c = nni_list_first(&s->send_queue)) != NULL) {
		nni_list_remove(&s->send_queue, c);
		mqtt_send_msg(c->saio, c);
		c->saio = NULL;
		nni_sleep_aio(s->retry, &p->time_aio);
		nni_pipe_recv(p->pipe, &p->recv_aio);
		return(0);
	}
	nni_mtx_unlock(&s->mtx);
	//initiate the global resend timer
	nni_sleep_aio(s->retry, &p->time_aio);
	nni_pipe_recv(p->pipe, &p->recv_aio);
	return (0);
}

static void
mqtt_pipe_stop(void *arg)
{
	mqtt_pipe_t *p = arg;
	nni_aio_stop(&p->send_aio);
	nni_aio_stop(&p->recv_aio);
	nni_aio_stop(&p->time_aio);
}

static void
mqtt_pipe_close(void *arg)
{
	mqtt_pipe_t *p = arg;
	mqtt_sock_t *s = p->mqtt_sock;

	nni_mtx_lock(&s->mtx);
	s->mqtt_pipe = NULL;
	nni_aio_close(&p->send_aio);
	nni_aio_close(&p->recv_aio);
	nni_aio_close(&p->time_aio);
	nni_lmq_flush(&p->recv_messages);
	nni_lmq_flush(&p->send_messages);

	bool is_sqlite = get_persist(s);
	if (!is_sqlite) {
		nni_id_map_foreach(p->sent_unack, mqtt_close_unack_msg_cb);
	}

	nni_id_map_foreach(&p->recv_unack, mqtt_close_unack_msg_cb);
	nni_mtx_unlock(&s->mtx);

	nni_atomic_set_bool(&p->closed, true);
}

static inline void
mqtt_pipe_recv_msgq_putq(mqtt_pipe_t *p, nni_msg *msg)
{
	if (0 != nni_lmq_put(&p->recv_messages, msg)) {
		// resize to ensure we do not lost messages or just lose it?
		// add option to drop messages
		// if (0 !=
		//     nni_lmq_resize(&p->recv_messages,
		//         nni_lmq_len(&p->recv_messages) * 2)) {
		// 	// drop the message when no memory available
		// 	nni_msg_free(msg);
		// 	return;
		// }
		// nni_lmq_put(&p->recv_messages, msg);
		nni_msg_free(msg);
	}
}

static void
flush_offline_cache(mqtt_sock_t *s)
{
#if defined(NNG_HAVE_MQTT_BROKER) && defined(NNG_SUPP_SQLITE)
	if (s->bridge_conf) {
		char *config_name = get_config_name(s);
		nni_mqtt_qos_db_set_client_offline_msg_batch(
		    s->sqlite_db, &s->offline_cache, config_name);
		nni_mqtt_qos_db_remove_oldest_client_offline_msg(s->sqlite_db,
		    s->bridge_conf->sqlite->disk_cache_size, config_name);
	}
#else
	NNI_ARG_UNUSED(s);
#endif
}

// Timer callback, we use it for retransmitting.
static void
mqtt_timer_cb(void *arg)
{
	mqtt_pipe_t *p = arg;
	mqtt_sock_t *s = p->mqtt_sock;
	nni_msg *  msg;
	nni_aio *  aio;
	uint16_t   pid = 0;

	if (nng_aio_result(&p->time_aio) != 0) {
		return;
	}
	nni_mtx_lock(&s->mtx);
	if (NULL == p || nni_atomic_get_bool(&p->closed)) {
		return;
	}
	// start message resending
	msg = nni_id_get_any(p->sent_unack, &pid);
	if (msg != NULL) {
		uint16_t ptype;
		ptype = nni_mqtt_msg_get_packet_type(msg);
		if (ptype == NNG_MQTT_PUBLISH) {
			nni_mqtt_msg_set_publish_dup(msg, true);
		}
		if (!p->busy) {
			p->busy = true;
			nni_msg_clone(msg);
			nni_mqtt_msg_encode(msg);
			aio = nni_mqtt_msg_get_aio(msg);
			if (aio) {
				nni_aio_bump_count(aio,
				    nni_msg_header_len(msg) +
				        nni_msg_len(msg));
				nni_aio_set_msg(aio, NULL);
			}
			nni_aio_set_msg(&p->send_aio, msg);
			nni_pipe_send(p->pipe, &p->send_aio);

			nni_mtx_unlock(&s->mtx);
			nni_sleep_aio(s->retry, &p->time_aio);
			return;
		} else {
			nni_msg_clone(msg);
			nni_lmq_put(&p->send_messages, msg);
		}
	}

	nni_mtx_unlock(&s->mtx);
	nni_sleep_aio(s->retry, &p->time_aio);
	return;
}

static void
mqtt_send_cb(void *arg)
{
	mqtt_pipe_t *p   = arg;
	mqtt_sock_t *s   = p->mqtt_sock;
	mqtt_ctx_t * c   = NULL;
	nni_msg *    msg = NULL;

	if (nni_aio_result(&p->send_aio) != 0) {
		// We failed to send... clean up and deal with it.
		nni_msg_free(nni_aio_get_msg(&p->send_aio));
		nni_aio_set_msg(&p->send_aio, NULL);
		s->disconnect_code = 0x8B;
		nni_pipe_close(p->pipe);
		return;
	}
	nni_mtx_lock(&s->mtx);

	p->busy = false;
	if (nni_atomic_get_bool(&s->closed) ||
	    nni_atomic_get_bool(&p->closed)) {
		// This occurs if the mqtt_pipe_close has been called.
		// In that case we don't want any more processing.
		nni_mtx_unlock(&s->mtx);
		return;
	}
	// Check cached ctx in nni_list first
	// these ctxs are triggered before the pipe is established
	if ((c = nni_list_first(&s->send_queue)) != NULL) {
		nni_list_remove(&s->send_queue, c);
		mqtt_send_msg(c->saio, c);
		c->saio = NULL;
		return;
	}
	if (nni_lmq_get(&p->send_messages, &msg) == 0) {
		p->busy = true;
		nni_mqtt_msg_encode(msg);
		nni_aio_set_msg(&p->send_aio, msg);
		nni_pipe_send(p->pipe, &p->send_aio);
		nni_mtx_unlock(&s->mtx);
		return;
	}

	if (NULL != (msg = get_cache_msg(s))) {
		p->busy = true;
		nni_aio_set_msg(&p->send_aio, msg);
		nni_pipe_send(p->pipe, &p->send_aio);
		nni_mtx_unlock(&s->mtx);
		return;
	}

	p->busy = false;
	nni_mtx_unlock(&s->mtx);
	return;
}

static void
mqtt_recv_cb(void *arg)
{
	mqtt_pipe_t *    p          = arg;
	mqtt_sock_t *    s          = p->mqtt_sock;
	nni_aio *        user_aio   = NULL;
	nni_msg *        cached_msg = NULL;
	mqtt_ctx_t *     ctx;

	if (nni_aio_result(&p->recv_aio) != 0) {
		s->disconnect_code = 0x8B;
		nni_pipe_close(p->pipe);
		return;
	}

	nni_mtx_lock(&s->mtx);
	nni_msg *msg = nni_aio_get_msg(&p->recv_aio);
	nni_aio_set_msg(&p->recv_aio, NULL);
	if (nni_atomic_get_bool(&s->closed) ||
	    nni_atomic_get_bool(&p->closed)) {
		//free msg and dont return data when pipe is closed.
		if (msg) {
			nni_msg_free(msg);
		}
		nni_mtx_unlock(&s->mtx);
		return;
	}
	nni_msg_set_pipe(msg, nni_pipe_id(p->pipe));
	nni_mqtt_msg_proto_data_alloc(msg);
	nni_mqtt_msg_decode(msg);

	packet_type_t packet_type = nni_mqtt_msg_get_packet_type(msg);
	int32_t       packet_id;
	uint8_t       qos;

	// schedule another receive
	nni_pipe_recv(p->pipe, &p->recv_aio);

	// state transitions
	switch (packet_type) {
	case NNG_MQTT_CONNACK:
		// never reach here
		nni_mtx_unlock(&s->mtx);
		return;
	case NNG_MQTT_PUBACK:
		// we have received a PUBACK, successful delivery of a QoS 1
		// FALLTHROUGH
	case NNG_MQTT_PUBCOMP:
		// we have received a PUBCOMP, successful delivery of a QoS 2
		// FALLTHROUGH
	case NNG_MQTT_SUBACK:
		// we have received a SUBACK, successful subscription
		// FALLTHROUGH
	case NNG_MQTT_UNSUBACK:
		// we have received a UNSUBACK, successful unsubscription
		packet_id  = nni_mqtt_msg_get_packet_id(msg);
		cached_msg = nni_id_get(p->sent_unack, packet_id);
		if (cached_msg != NULL) {
			nni_id_remove(p->sent_unack, packet_id);
			user_aio = nni_mqtt_msg_get_aio(cached_msg);
			if (packet_type == NNG_MQTT_SUBACK ||
			    packet_type == NNG_MQTT_UNSUBACK) {
				nni_msg_clone(msg);
				nni_aio_set_msg(user_aio, msg);
			}
			nni_msg_free(cached_msg);
		}
		nni_msg_free(msg);
		break;

	case NNG_MQTT_PINGRESP:
		// free msg
		nni_msg_free(msg);
		nni_mtx_unlock(&s->mtx);
		return;

	case NNG_MQTT_PUBREC:
		nni_msg_free(msg);
		break;

	case NNG_MQTT_PUBREL:
		packet_id = nni_mqtt_msg_get_pubrel_packet_id(msg);
		cached_msg = nni_id_get(&p->recv_unack, packet_id);
		nni_msg_free(msg);
		if (cached_msg == NULL) {
			nni_plat_printf("ERROR! packet id %d not found\n", packet_id);
			break;
		}
		nni_id_remove(&p->recv_unack, packet_id);

		if ((ctx = nni_list_first(&s->recv_queue)) == NULL) {
			// No one waiting to receive yet, putting msg
			// into lmq
			mqtt_pipe_recv_msgq_putq(p, cached_msg);
			nni_mtx_unlock(&s->mtx);
			// nni_println("ERROR: no ctx found!! create more ctxs!");
			return;
		}
		nni_list_remove(&s->recv_queue, ctx);
		user_aio  = ctx->raio;
		ctx->raio = NULL;
		nni_aio_set_msg(user_aio, cached_msg);
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish(user_aio, 0, 0);
		return;

	case NNG_MQTT_PUBLISH:
		// we have received a PUBLISH
		qos = nni_mqtt_msg_get_publish_qos(msg);
		if (2 > qos) {
			// QoS 0, successful receipt
			// QoS 1, the transport handled sending a PUBACK
			if ((ctx = nni_list_first(&s->recv_queue)) == NULL) {
				// No one waiting to receive yet, putting msg
				// into lmq
				mqtt_pipe_recv_msgq_putq(p, msg);
				nni_mtx_unlock(&s->mtx);
				// nni_println("ERROR: no ctx found!! create more ctxs!");
				return;
			}
			nni_list_remove(&s->recv_queue, ctx);
			user_aio = ctx->raio;
			ctx->raio = NULL;
			nni_aio_set_msg(user_aio, msg);
			nni_mtx_unlock(&s->mtx);
			nni_aio_finish(user_aio, 0, 0);
			return;
		} else {
			//TODO check if this packetid already there
			packet_id = nni_mqtt_msg_get_publish_packet_id(msg);
			if ((cached_msg = nni_id_get(
				         &p->recv_unack, packet_id)) != NULL) {
					// packetid already exists.
					// sth wrong with the broker
					// replace old with new
					nni_plat_printf(
					    "ERROR: packet id %d duplicates in", packet_id);
					nni_msg_free(cached_msg);
					// nni_id_remove(&pipe->nano_qos_db,
					// pid);
				}
			nni_id_set(&p->recv_unack, packet_id, msg);
		}
		break;

	default:
		// unexpected packet type, server misbehaviour
		nni_mtx_unlock(&s->mtx);
		s->disconnect_code = 0x81;
		nni_pipe_close(p->pipe);
		return;
	}

	nni_mtx_unlock(&s->mtx);
	if (user_aio) {
		nni_aio_finish(user_aio, 0, 0);
	}

	return;
}

/******************************************************************************
 *                           Context Implementation                           *
 ******************************************************************************/

static void
mqtt_ctx_init(void *arg, void *sock)
{
	mqtt_ctx_t * ctx = arg;
	mqtt_sock_t *s   = sock;

	ctx->mqtt_sock = s;
	NNI_LIST_NODE_INIT(&ctx->sqnode);
	NNI_LIST_NODE_INIT(&ctx->rqnode);
}

static void
mqtt_ctx_fini(void *arg)
{
	mqtt_ctx_t * ctx = arg;
	mqtt_sock_t *s   = ctx->mqtt_sock;
	nni_aio *  aio;

	nni_mtx_lock(&s->mtx);
	if (nni_list_active(&s->send_queue, ctx)) {
		if ((aio = ctx->saio) != NULL) {
			ctx->saio = NULL;
			nni_list_remove(&s->send_queue, ctx);
			nni_aio_finish_error(aio, NNG_ECLOSED);
		}
	} else if (nni_list_active(&s->recv_queue, ctx)) {
		if ((aio = ctx->raio) != NULL) {
			ctx->raio = NULL;
			nni_list_remove(&s->recv_queue, ctx);
			nni_aio_finish_error(aio, NNG_ECLOSED);
		}
	}
	nni_mtx_unlock(&s->mtx);
}

static void
mqtt_ctx_send(void *arg, nni_aio *aio)
{
	mqtt_ctx_t * ctx = arg;
	mqtt_sock_t *s   = ctx->mqtt_sock;
	mqtt_pipe_t *p   = s->mqtt_pipe;
	nni_msg *    msg;

	if (nni_aio_begin(aio) != 0) {
		return;
	}

	nni_mtx_lock(&s->mtx);

	if (nni_atomic_get_bool(&s->closed)) {
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}

	msg   = nni_aio_get_msg(aio);
	if (msg == NULL) {
		nni_mtx_unlock(&s->mtx);
		nni_aio_set_msg(aio, NULL);
		nni_aio_finish_error(aio, NNG_EPROTO);
		return;
	}
	if (p == NULL) {
		// connection is lost or not established yet
#if defined(NNG_HAVE_MQTT_BROKER) && defined(NNG_SUPP_SQLITE)
		conf_bridge_node *bridge = s->bridge_conf;
		if (bridge != NULL && bridge->enable &&
		    bridge->sqlite->enable) {
			// the msg order is exactly as same as the ctx
			// in send_queue
			nni_lmq_put(&s->offline_cache, msg);
			if (nni_lmq_full(&s->offline_cache)) {
				flush_offline_cache(s);
			}
			nni_mtx_unlock(&s->mtx);
			nni_aio_set_msg(aio, NULL);
			nni_aio_finish_error(aio, NNG_ECLOSED);
			return;
		}
#endif

		if (!nni_list_active(&s->send_queue, ctx)) {
			// cache ctx
			ctx->saio = aio;
			nni_list_append(&s->send_queue, ctx);
			nni_mtx_unlock(&s->mtx);
			debug_msg("WARNING:client sending msg while disconnected! cached");
		} else {
			nni_msg_free(msg);
			nni_mtx_unlock(&s->mtx);
			nni_aio_set_msg(aio, NULL);
			nni_aio_finish_error(aio, NNG_ECLOSED);
			debug_msg("WARNING:client sending msg while disconnected! dropped");
		}
		return;
	}
	mqtt_send_msg(aio, ctx);
	debug_msg("client sending msg now");
	return;
}

static void
mqtt_ctx_recv(void *arg, nni_aio *aio)
{
	mqtt_ctx_t * ctx = arg;
	mqtt_sock_t *s   = ctx->mqtt_sock;
	mqtt_pipe_t *p   = s->mqtt_pipe;
	nni_msg     *msg = NULL;

	if (nni_aio_begin(aio) != 0) {
		return;
	}

	nni_mtx_lock(&s->mtx);
	if ( p == NULL ) {
		goto wait;
	} 
	if (nni_atomic_get_bool(&s->closed) || nni_atomic_get_bool(&p->closed)) {
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}

	if (nni_lmq_get(&p->recv_messages, &msg) == 0) {
		nni_aio_set_msg(aio, msg);
		nni_mtx_unlock(&s->mtx);
		//let user gets a quick reply
		nni_aio_finish(aio, 0, nni_msg_len(msg));
		return;
	}

	// no open pipe or msg wating
wait:
	if (ctx->raio != NULL) {
		nni_mtx_unlock(&s->mtx);
		// nni_println("ERROR! former aio not finished!");
		nni_aio_finish_error(aio, NNG_ESTATE);
		return;
	}
	ctx->raio = aio;
	nni_list_append(&s->recv_queue, ctx);
	nni_mtx_unlock(&s->mtx);
	return;
}

static inline bool
get_persist(mqtt_sock_t *s)
{
#ifdef NNG_HAVE_MQTT_BROKER
	return s->bridge_conf != NULL ? s->bridge_conf->sqlite->enable : false;
#else
	NNI_ARG_UNUSED(s);
	return false;
#endif
}

static inline char *
get_config_name(mqtt_sock_t *s)
{
#ifdef NNG_HAVE_MQTT_BROKER
	return s->bridge_conf != NULL ? s->bridge_conf->name : NULL;
#else
	NNI_ARG_UNUSED(s);
	return NULL;
#endif
}

static nni_proto_pipe_ops mqtt_pipe_ops = {
	.pipe_size  = sizeof(mqtt_pipe_t),
	.pipe_init  = mqtt_pipe_init,
	.pipe_fini  = mqtt_pipe_fini,
	.pipe_start = mqtt_pipe_start,
	.pipe_close = mqtt_pipe_close,
	.pipe_stop  = mqtt_pipe_stop,
};

static nni_option mqtt_ctx_options[] = {
	{
	    .o_name = NULL,
	},
};

static nni_proto_ctx_ops mqtt_ctx_ops = {
	.ctx_size    = sizeof(mqtt_ctx_t),
	.ctx_init    = mqtt_ctx_init,
	.ctx_fini    = mqtt_ctx_fini,
	.ctx_recv    = mqtt_ctx_recv,
	.ctx_send    = mqtt_ctx_send,
	.ctx_options = mqtt_ctx_options,
};

static nni_option mqtt_sock_options[] = {
	{
	    .o_name = NNG_OPT_MQTT_DISCONNECT_REASON,
	    .o_get  = mqtt_sock_get_disconnect_code,
	},
	{
	    .o_name = NNG_OPT_MQTT_DISCONNECT_PROPERTY,
	    .o_get  = mqtt_sock_get_disconnect_prop,
	},
	{
	    .o_name = NANO_CONF,
	    .o_set  = mqtt_sock_set_conf_with_db,
	},
	// terminate list
	{
	    .o_name = NULL,
	},
};

static nni_proto_sock_ops mqtt_sock_ops = {
	.sock_size    = sizeof(mqtt_sock_t),
	.sock_init    = mqtt_sock_init,
	.sock_fini    = mqtt_sock_fini,
	.sock_open    = mqtt_sock_open,
	.sock_close   = mqtt_sock_close,
	.sock_options = mqtt_sock_options,
	.sock_send    = mqtt_sock_send,
	.sock_recv    = mqtt_sock_recv,
};

static nni_proto mqtt_proto = {
	.proto_version  = NNI_PROTOCOL_VERSION,
	.proto_self     = { NNG_MQTT_SELF, NNG_MQTT_SELF_NAME },
	.proto_peer     = { NNG_MQTT_PEER, NNG_MQTT_PEER_NAME },
	.proto_flags    = NNI_PROTO_FLAG_SNDRCV,
	.proto_sock_ops = &mqtt_sock_ops,
	.proto_pipe_ops = &mqtt_pipe_ops,
	.proto_ctx_ops  = &mqtt_ctx_ops,
};

int
nng_mqtt_client_open(nng_socket *sock)
{
	return (nni_proto_open(sock, &mqtt_proto));
}
