//
// Copyright 2022 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <string.h>

#include "core/nng_impl.h"
#include "core/sockimpl.h"
#include "nng/nng.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/protocol/mqtt/nmq_mqtt.h"
#include "supplemental/mqtt/mqtt_msg.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/hash_table.h"
#include "nng/supplemental/nanolib/mqtt_db.h"
#include "supplemental/mqtt/mqtt_qos_db_api.h"

#define DB_NAME "nano_qos_db.db"

typedef struct nano_pipe   nano_pipe;
typedef struct nano_sock   nano_sock;
typedef struct nano_ctx    nano_ctx;
typedef struct cs_msg_list cs_msg_list;

static void        nano_pipe_send_cb(void *);
static void        nano_pipe_recv_cb(void *);
static void        nano_pipe_fini(void *);
static int         nano_pipe_close(void *);
static inline void close_pipe(nano_pipe *p);

static uint16_t tmp_id = 1000;
static uint32_t rotate = 0;
// huge context/ dynamic context?
struct nano_ctx {
	nano_sock *sock;
	uint32_t   pipe_id;
	// when resending
	nano_pipe    *spipe, *qos_pipe; // send pipe
	nni_aio      *saio;             // send aio
	nni_aio      *raio;             // recv aio
	nni_list_node sqnode;
	nni_list_node rqnode;
};

// nano_sock is our per-socket protocol private structure.
struct nano_sock {
	nni_mtx        lk;
	nni_msg       *pingmsg;
	nni_atomic_int ttl;
	nni_id_map     pipes;
	nni_id_map     cached_sessions;
	nni_lmq        waitlmq;   // this is for receving
	nni_list       recvpipes; // list of pipes with data to receive
	nni_list       recvq;
	nano_ctx       ctx; // base socket
	nni_pollable   readable;
	nni_pollable   writable;
	conf          *conf;
	void          *db;
#ifdef NNG_SUPP_SQLITE
	sqlite3 *sqlite_db;
#endif
};

// nano_pipe is our per-pipe protocol private structure.
struct nano_pipe {
	nni_mtx     lk;
	nni_pipe   *pipe;
	nano_sock  *broker;
	conn_param *conn_param;
	nni_lmq     rlmq; // only for sending cache
	uint8_t     reason_code;
	uint32_t    id;  // pipe id of nni_pipe
	uint16_t    rid; // index of packet ID for resending
	uint16_t    keepalive;
	uint16_t 	ka_refresh; // ka_refresh count how many times the keepalive
	                     	// timer has been triggered
	bool          busy;
	bool          event; // indicates if exposure disconnect event is valid
	void         *tree;  // root node of db tree
	void         *nano_qos_db; // 'sqlite' or 'nni_id_hash_map'
	nni_aio       aio_send;
	nni_aio       aio_recv;
	nni_aio       aio_timer;
	nni_list_node rnode; // receivable list linkage
	nni_atomic_bool closed;
};

void
nmq_close_unack_msg_cb(void *key, void *val)
{
	NNI_ARG_UNUSED(key);

	nni_msg *msg = val;
	nni_msg_free(msg);
}

// Flush lmq and conn_param
void
nano_nni_lmq_flush(nni_lmq *lmq, bool cp)
{
	while (lmq->lmq_len > 0) {
		nng_msg *msg = lmq->lmq_msgs[lmq->lmq_get++];
		lmq->lmq_get &= lmq->lmq_mask;
		lmq->lmq_len--;
		if (cp)
			conn_param_free(nni_msg_get_conn_param(msg));
		nni_msg_free(msg);
	}
}


// only use for sending lmq
static int
nano_nni_lmq_resize(nni_lmq *lmq, size_t cap)
{
	nng_msg  *msg;
	nng_msg **newq;
	size_t    alloc;
	size_t    len;

	alloc = 2;
	while (alloc < cap) {
		alloc *= 2;
	}

	newq = nni_alloc(sizeof(nng_msg *) * alloc);
	if (newq == NULL) {
		return (NNG_ENOMEM);
	}

	len = 0;
	while ((len < cap) && (nni_lmq_get(lmq, &msg) == 0)) {
		newq[len++] = msg;
	}

	// Flush anything left over.
	nano_nni_lmq_flush(lmq, false);

	nni_free(lmq->lmq_msgs, lmq->lmq_alloc * sizeof(nng_msg *));
	lmq->lmq_msgs  = newq;
	lmq->lmq_cap   = cap;
	lmq->lmq_alloc = alloc;
	lmq->lmq_mask  = alloc - 1;
	lmq->lmq_len   = len;
	lmq->lmq_put   = len;
	lmq->lmq_get   = 0;

	return (0);
}

void
nano_nni_lmq_fini(nni_lmq *lmq)
{
	if (lmq == NULL) {
		return;
	}

	/* Free any orphaned messages. */
	while (lmq->lmq_len > 0) {
		nng_msg *msg = lmq->lmq_msgs[lmq->lmq_get++];
		lmq->lmq_get &= lmq->lmq_mask;
		lmq->lmq_len--;
		nni_msg_free(msg);
	}

	nni_free(lmq->lmq_msgs, lmq->lmq_alloc * sizeof(nng_msg *));
}

static void
nano_pipe_timer_cb(void *arg)
{
	nano_pipe *      p            = arg;
	uint32_t         qos_duration = p->broker->conf->qos_duration;
	float            qos_backoff  = p->broker->conf->backoff;
	nni_pipe        *npipe        = p->pipe;
	nni_time         time;
	int 		 rv = 0;

	bool is_sqlite = p->broker->conf->sqlite.enable;

	if (nng_aio_result(&p->aio_timer) != 0) {
		return;
	}
	nni_mtx_lock(&p->lk);
	// TODO pipe lock or sock lock?
	if (npipe->cache) {
		nng_time will_intval = p->conn_param->will_delay_interval;
		nng_time session_int = p->conn_param->session_expiry_interval;
		p->ka_refresh++;
		time = (uint64_t)(p->ka_refresh * (qos_duration));
		rv += will_intval > 0 ? (nng_clock() > will_intval ? 1 : 0) : 0;
		if (p->conn_param->pro_ver == MQTT_VERSION_V311)
			rv += session_int > 0 ? (time > session_int ? 1 : 0) : 0;
		else
			rv += (time > session_int ? 1 : 0);
		// check session expiry interval
		log_trace("check session alive time %lu", time);
		if (rv) {
			// EXPIRED. close pipe & clean previous session
			nano_pipe *old;
			nano_sock *s = p->broker;

			old = nni_id_get(&s->cached_sessions, p->pipe->p_id);
			if (old != NULL) {
				old->event       = true;
				old->pipe->cache = false;
#ifdef NNG_SUPP_SQLITE
				nni_qos_db_remove_by_pipe(is_sqlite,
				    old->nano_qos_db, old->pipe->p_id);
				nni_qos_db_remove_pipe(is_sqlite,
				    old->nano_qos_db, old->pipe->p_id);
				nni_qos_db_remove_unused_msg(
				    is_sqlite, old->nano_qos_db);
#endif
				nni_id_remove(
				    &s->cached_sessions, p->pipe->p_id);
			}
			p->reason_code = 0x8E;
			nni_mtx_unlock(&p->lk);
			nni_pipe_close(p->pipe);
			return;
		}
		nni_sleep_aio(qos_duration * 1000, &p->aio_timer);
		nni_mtx_unlock(&p->lk);
		return;
	}
	qos_backoff = p->ka_refresh * (qos_duration) *1000 -
	    p->keepalive * qos_backoff * 1000;
	log_trace("check pipe keepalive interval %d backoff %f, ka %d",
			  p->keepalive, qos_backoff, p->ka_refresh);
	if (qos_backoff > 0) {
		log_warn(
		    "Warning: close pipe & kick client due to KeepAlive "
		    "timeout!");
		p->reason_code = NMQ_KEEP_ALIVE_TIMEOUT;
		nni_sleep_aio(qos_duration * 1000, &p->aio_timer);
		nni_mtx_unlock(&p->lk);
		p->reason_code = KEEP_ALIVE_TIMEOUT;
		nni_pipe_close(p->pipe);
		return;
	}
	p->ka_refresh++;

	if (!p->busy) {
		nni_msg *msg, *rmsg;
		uint16_t pid = p->rid;

		// trying to resend msg
		msg = nni_qos_db_get_one(
		    is_sqlite, npipe->nano_qos_db, npipe->p_id, &pid);
		if (msg != NULL) {
			rmsg = msg;
			property      *prop = NULL;
			property_data *data = NULL;
			if (p->conn_param->pro_ver == MQTT_PROTOCOL_VERSION_v5) {
				if (nni_msg_get_proto_data(rmsg) != NULL)
					prop = nni_mqtt_msg_get_publish_property(rmsg);
			}
			if (prop) {
				data = property_get_value(prop, MESSAGE_EXPIRY_INTERVAL);
			}
			nni_time ntime = nni_clock();
			nni_time mtime = nni_msg_get_timestamp(rmsg);
			if (data && ntime > mtime + data->p_value.u32 * 1000) {
				log_info("QoS msg %d expired!", pid);
				// remove expired msg from qos db
				nni_qos_db_remove_msg(
				    is_sqlite, npipe->nano_qos_db, rmsg);
				nni_qos_db_remove(is_sqlite,
				    npipe->nano_qos_db, npipe->p_id, pid);
			} else if ((ntime - mtime) >=
			    (long unsigned) qos_duration * 1250) {
				p->busy = true;
				// TODO set max retrying times in nanomq.conf
				nano_msg_set_dup(rmsg);
				// deliver packet id to transport here
				nni_aio_set_prov_data(&p->aio_send, (void *)(uintptr_t)pid);
				// put original msg into sending
				nni_msg_clone(rmsg);
				nni_aio_set_msg(&p->aio_send, rmsg);
				log_info("resending qos msg packetid: %d", pid);
				nni_pipe_send(p->pipe, &p->aio_send);
			}
		}
	}
	nni_sleep_aio(qos_duration * 1000, &p->aio_timer);
	nni_mtx_unlock(&p->lk);
	return;
}

static void
nano_ctx_close(void *arg)
{
	nano_ctx  *ctx = arg;
	nano_sock *s   = ctx->sock;
	nni_aio   *aio;

	log_trace("nano_ctx_close");
	nni_mtx_lock(&s->lk);
	if ((aio = ctx->saio) != NULL) {
		// nano_pipe *pipe = ctx->spipe;
		ctx->saio     = NULL;
		ctx->spipe    = NULL;
		ctx->qos_pipe = NULL;
		nni_aio_finish_error(aio, NNG_ECLOSED);
	}
	if ((aio = ctx->raio) != NULL) {
		nni_list_remove(&s->recvq, ctx);
		ctx->raio = NULL;
		nni_aio_finish_error(aio, NNG_ECLOSED);
	}
	nni_mtx_unlock(&s->lk);
}

static void
nano_ctx_fini(void *arg)
{
	nano_ctx *ctx = arg;

	nano_ctx_close(ctx);
	log_trace("========= nano_ctx_fini =========");
}

static void
nano_ctx_init(void *carg, void *sarg)
{
	nano_sock *s   = sarg;
	nano_ctx  *ctx = carg;

	log_trace("&&&&&&&& nano_ctx_init %p &&&&&&&&&", ctx);
	NNI_LIST_NODE_INIT(&ctx->sqnode);
	NNI_LIST_NODE_INIT(&ctx->rqnode);

	ctx->sock    = s;
	ctx->pipe_id = 0;
}

static void
nano_ctx_cancel_send(nni_aio *aio, void *arg, int rv)
{
	nano_ctx  *ctx = arg;
	nano_sock *s   = ctx->sock;

	log_trace("*********** nano_ctx_cancel_send ***********");
	nni_mtx_lock(&s->lk);
	if (ctx->saio != aio) {
		nni_mtx_unlock(&s->lk);
		return;
	}
	nni_list_node_remove(&ctx->sqnode);
	ctx->saio = NULL;
	nni_mtx_unlock(&s->lk);

	nni_msg_header_clear(nni_aio_get_msg(aio)); // reset the headers
	nni_aio_finish_error(aio, rv);
}

static void
nano_ctx_send(void *arg, nni_aio *aio)
{
	nano_ctx  *ctx = arg;
	nano_sock *s   = ctx->sock;
	nano_pipe *p;
	nni_msg   *msg;
	int        rv;
	uint32_t   pipe = 0;
	uint32_t  *pipeid;
	uint8_t    qos_pac = 0, qos = 0;
	char      *pld_pac  = NULL;
	int        tlen_pac = 0;
	uint16_t   packetid;

	bool is_sqlite = s->conf->sqlite.enable;

	msg = nni_aio_get_msg(aio);

	if (nni_aio_begin(aio) != 0) {
		nni_msg_free(msg);
		log_error("Aio misuesed!");
		return;
	}

	log_trace(" #### nano_ctx_send with ctx %p msg type %x #### ", ctx,
	    nni_msg_get_type(msg));

	pipeid = nni_aio_get_prov_data(aio);
	if (pipeid)
		pipe = *pipeid;
	nni_aio_set_prov_data(aio, NULL);
	if (ctx == &s->ctx) {
		nni_pollable_clear(&s->writable);
	}

	nni_mtx_lock(&s->lk);
	log_trace(" ******** working with pipe id : %d ctx ******** ", pipe);
	if ((p = nni_id_get(&s->pipes, pipe)) == NULL) {
		// pre-configured session
		void *qos_db = NULL;
		if (s->conf->ext_qos_db)
			 qos_db = nng_id_get(s->conf->ext_qos_db, pipe);
		if (qos_db != NULL) {
			if (nni_msg_get_type(msg) == CMD_PUBLISH &&
			    nni_msg_get_pub_qos(msg) > 0) {
				nni_msg_clone(msg); // for line 422
				nni_qos_db_set(is_sqlite, qos_db,
				    pipe, tmp_id, msg);
				tmp_id ++;
				log_debug("msg cached for preset session %d", pipe);
			}
		}
		// Pipe is gone.  Make this look like a good send to avoid
		// disrupting the state machine.  We don't care if the peer
		// lost interest in our reply.
		nni_mtx_unlock(&s->lk);
		nni_aio_set_msg(aio, NULL);
		log_warn("pipe id %ld is gone, pub failed", pipe);
		nni_msg_free(msg);
		return;
	}

	// 2 locks here cause performance degradation
	nni_mtx_lock(&p->lk);
	nni_mtx_unlock(&s->lk);

	if (p->pipe->cache) {
		if (nni_msg_get_type(msg) == CMD_PUBLISH) {
			qos_pac = nni_msg_get_pub_qos(msg);
			pld_pac = nni_msg_get_pub_topic(msg, &tlen_pac);
		}
		subinfo *info = NULL;
		NNI_LIST_FOREACH(p->pipe->subinfol, info) {
			if (!info)
				continue;
			if (topic_filtern(info->topic, pld_pac, tlen_pac)) {
				qos = qos_pac > info->qos ? info->qos : qos_pac; // MIN
				break;
			}
		}
		if (qos > 0) {
			packetid = nni_pipe_inc_packetid(p->pipe);
			// TODO potential qos msg overwrite?
			nni_qos_db_set(is_sqlite, p->pipe->nano_qos_db,
			    p->pipe->p_id, packetid, msg);
			nni_qos_db_remove_oldest(is_sqlite,
			    p->pipe->nano_qos_db,
			    s->conf->sqlite.disk_cache_size);
			log_debug("msg cached for session");
		} else {
			// only cache QoS messages
			log_debug("Drop msg due to qos == 0");
			nni_msg_free(msg);
		}
		nni_mtx_unlock(&p->lk);
		nni_aio_set_msg(aio, NULL);
		return;
	}

	if (!p->busy) {
		p->busy = true;
		nni_aio_set_msg(&p->aio_send, msg);
		nni_pipe_send(p->pipe, &p->aio_send);
		nni_mtx_unlock(&p->lk);
		nni_aio_set_msg(aio, NULL);
		return;
	}

	if ((rv = nni_aio_schedule(aio, nano_ctx_cancel_send, ctx)) != 0) {
		nni_msg_free(msg);
		nni_mtx_unlock(&p->lk);
		return;
	}
	log_debug("pipe %d occupied! resending in cb!", pipe);
	if (nni_lmq_full(&p->rlmq)) {
		// Make space for the new message.
		if (nni_lmq_cap(&p->rlmq) <= NANO_MAX_QOS_PACKET) {
			if ((rv = nano_nni_lmq_resize(
			         &p->rlmq, nni_lmq_cap(&p->rlmq) * 2)) != 0) {
				log_warn("warning msg dropped!");
				nni_msg *old;
				nni_lmq_get(&p->rlmq, &old);
				nni_msg_free(old);
			}
		} else {
			// Warning msg lost due to reach the limit of lmq
			log_warn(
			    "Warning: msg lost due to reach the limit of lmq");
			nni_msg_free(msg);
			nni_mtx_unlock(&p->lk);
			nni_aio_set_msg(aio, NULL);
			return;
		}
	}

	nni_lmq_put(&p->rlmq, msg);

	nni_mtx_unlock(&p->lk);
	nni_aio_set_msg(aio, NULL);
	return;
}

static void
nano_sock_fini(void *arg)
{
	nano_sock *s = arg;
#ifdef NNG_SUPP_SQLITE
	if (s->conf->sqlite.enable) {
		nni_qos_db_fini_sqlite(s->sqlite_db);
	}
#endif
	nni_id_map_fini(&s->pipes);
	nni_id_map_fini(&s->cached_sessions);
	// flush msg and conn params in waitlmq
	nano_nni_lmq_flush(&s->waitlmq, true);
	nni_lmq_fini(&s->waitlmq);
	nano_ctx_fini(&s->ctx);
	nni_pollable_fini(&s->writable);
	nni_pollable_fini(&s->readable);
	nni_mtx_fini(&s->lk);

	conf_fini(s->conf);
	nni_msg_free(s->pingmsg);
}

static void
nano_sock_init(void *arg, nni_sock *sock)
{
	nano_sock *s = arg;

	NNI_ARG_UNUSED(sock);

	nni_mtx_init(&s->lk);

	nni_id_map_init(&s->pipes, 0, 0, false);
	nni_id_map_init(&s->cached_sessions, 0, 0, false);
	nni_lmq_init(&s->waitlmq, 256);
	NNI_LIST_INIT(&s->recvq, nano_ctx, rqnode);
	NNI_LIST_INIT(&s->recvpipes, nano_pipe, rnode);

	nni_atomic_init(&s->ttl);
	nni_atomic_set(&s->ttl, 8);

	(void) nano_ctx_init(&s->ctx, s);

	log_trace("************* nano_sock_init %p *************", s);
	// We start off without being either readable or writable.
	// Readability comes when there is something on the socket.
	nni_pollable_init(&s->writable);
	nni_pollable_init(&s->readable);

	s->pingmsg   = NULL;
	nni_msg_alloc(&s->pingmsg, 0);
	if (!s->pingmsg) {
		log_error("Error in create a pingmsg");
	} else {
		uint8_t buf[2];
		buf[0] = 0xD0;
		buf[1] = 0x00;
		nni_msg_header_append(s->pingmsg, buf, 2);
	}
}

static void
nano_sock_open(void *arg)
{
	NNI_ARG_UNUSED(arg);
}

static void
nano_sock_close(void *arg)
{
	nano_sock *s = arg;

	nano_ctx_close(&s->ctx);
}

static void
nano_pipe_stop(void *arg)
{
	nano_pipe *p = arg;
	if (p->pipe->cache)
		return; // your time is yet to come

	log_trace(" ########## nano_pipe_stop ########## ");
	// nni_aio_abort(&p->aio_send, NNG_ECANCELED);
	nni_aio_stop(&p->aio_send);
	nni_aio_stop(&p->aio_timer);
	nni_aio_stop(&p->aio_recv);
}

static void
nano_pipe_fini(void *arg)
{
	nano_pipe *p = arg;
	nano_sock *s = p->broker;
	nng_msg   *msg;

	log_trace(" ########## nano_pipe_fini ########## ");
	if (p->pipe->cache) {
		return; // your time is yet to come
	}
	nni_mtx_lock(&s->lk);
	nni_mtx_lock(&p->lk);
	if ((msg = nni_aio_get_msg(&p->aio_recv)) != NULL) {
		nni_aio_set_msg(&p->aio_recv, NULL);
	}
	if ((msg = nni_aio_get_msg(&p->aio_send)) != NULL) {
		nni_aio_set_msg(&p->aio_send, NULL);
		nni_msg_free(msg);
	}

	//Safely free the msgs in qos_db, only when nano_qos_db is not taken by new pipe
	void *nano_qos_db = p->pipe->nano_qos_db;
	if (p->event == true) {
		if (!p->broker->conf->sqlite.enable && nano_qos_db != NULL) {
			nni_qos_db_remove_all_msg(false,
				nano_qos_db, nmq_close_unack_msg_cb);
			nni_qos_db_fini_id_hash(nano_qos_db);
			p->pipe->nano_qos_db = NULL;
		}
	} else {
		// we keep all structs in broker layer, except this conn_param
		conn_param_free(p->conn_param);
	}
	nni_mtx_unlock(&p->lk);

	nni_mtx_fini(&p->lk);
	nni_mtx_unlock(&s->lk);

	if (nni_aio_busy(&p->aio_send)) {
		nni_aio_abort(&p->aio_send, NNG_ECANCELED);
		nni_aio_close(&p->aio_send);
		log_error("send aio not finished!");
	}
	nni_aio_fini(&p->aio_send);
	nni_aio_fini(&p->aio_recv);
	nni_aio_fini(&p->aio_timer);
	nano_nni_lmq_fini(&p->rlmq);
}

static int
nano_pipe_init(void *arg, nni_pipe *pipe, void *s)
{
	nano_pipe *p    = arg;
	nano_sock *sock = s;

	log_trace("##########nano_pipe_init###############");

	nni_mtx_init(&p->lk);
	nni_lmq_init(&p->rlmq, sock->conf->msq_len);
	nni_aio_init(&p->aio_send, nano_pipe_send_cb, p);
	nni_aio_init(&p->aio_timer, nano_pipe_timer_cb, p);
	nni_aio_init(&p->aio_recv, nano_pipe_recv_cb, p);

	p->conn_param  = nni_pipe_get_conn_param(pipe);
	conn_param_free(p->conn_param);
	p->id          = nni_pipe_id(pipe);
	p->rid         = 1;
	p->pipe        = pipe;
	p->reason_code = 0x00;
	p->broker      = s;
	p->ka_refresh  = 0;
	p->event       = true;
	p->tree        = sock->db;
	if (p->conn_param != NULL)
		p->keepalive   = p->conn_param->keepalive_mqtt;
	if (!p->broker->conf->sqlite.enable && pipe->nano_qos_db == NULL) {
		nni_qos_db_init_id_hash(pipe->nano_qos_db);
	}

	return (0);
}

static int
nano_pipe_start(void *arg)
{
	char      *clientid;
	nano_pipe *p = arg;
	nano_pipe *old = NULL;
	nano_sock *s = p->broker;
	nni_msg   *msg;
	uint8_t    rv; // reason code of CONNACK
	nni_pipe  *npipe = p->pipe;

	bool is_sqlite = s->conf->sqlite.enable;

	log_trace(" ########## nano_pipe_start ########## ");

	nni_msg_alloc(&msg, 0);
	nni_mtx_lock(&s->lk);

#ifdef NNG_SUPP_SQLITE
	if (is_sqlite) {
		npipe->nano_qos_db = s->sqlite_db;
		p->nano_qos_db     = s->sqlite_db;
	}
#endif
	// Get IPv4 ADDR of client
	nng_sockaddr addr;
	uint8_t     *arr;
	nng_pipe     nng_pipe;
	nng_pipe.id = npipe->p_id;

	rv = nng_pipe_get_addr(nng_pipe, NNG_OPT_REMADDR, &addr);
	// TODO: addr.s_in.sa_port
	if (addr.s_family == NNG_AF_INET) {
		arr = (uint8_t *) &addr.s_in.sa_addr;
		if (arr == NULL) {
			log_warn("Fail to get IP addr from client pipe!");
			goto session_keeping;
		}
		sprintf(p->conn_param->ip_addr_v4, "%d.%d.%d.%d", arr[0],
		    arr[1], arr[2], arr[3]);
	} else if (addr.s_family == NNG_AF_INET6) {
		arr = (uint8_t *) &addr.s_in6.sa_addr;
		log_warn("IPv6 address is not supported in event msg yet");
	}

	log_debug("client connected! addr [%s port [%d]\n",
	    p->conn_param->ip_addr_v4, addr.s_in.sa_port);

session_keeping:
	// Clientid should not be NULL since broker will assign one
	// TODO use p_id
	clientid = (char *) conn_param_get_clientid(p->conn_param);
	if (!clientid) {
		log_warn("NULL clientid found when try to restore session.");
		nni_mtx_unlock(&s->lk);
		return NNG_ECONNSHUT;
	}
	if (p->conn_param->clean_start == 0) {
		old = nni_id_get(&s->cached_sessions, p->pipe->p_id);
		if (old != NULL) {
			// replace nano_qos_db and pid with old one.
			p->pipe->packet_id = old->pipe->packet_id;
			// there should be no msg in this map

			if (!is_sqlite && p->pipe->nano_qos_db!= NULL) {
				nni_qos_db_fini_id_hash(p->pipe->nano_qos_db);
				p->pipe->nano_qos_db = NULL;
			}

			p->pipe->nano_qos_db = old->nano_qos_db;
			log_info("resuming session %d with %d", npipe->p_id, old->pipe->p_id);
			nni_list *l = npipe->subinfol;
			npipe->subinfol = old->pipe->subinfol;
			old->pipe->subinfol = l;
			p->id = nni_pipe_id(npipe);
			// set event to false so that no notification will be sent
			p->event = false;
			// set event of old pipe to false and discard it.
			old->event       = false;
			old->pipe->cache = false;
			nni_id_remove(&s->cached_sessions, p->pipe->p_id);
		}
	} else {
		// clean previous session
		old = nni_id_get(&s->cached_sessions, p->pipe->p_id);
		if (old != NULL) {
			old->event       = true;
			old->pipe->cache = false;
#ifdef NNG_SUPP_SQLITE
			nni_qos_db_remove_by_pipe(
			    is_sqlite, old->nano_qos_db, old->pipe->p_id);
			nni_qos_db_remove_pipe(
			    is_sqlite, old->nano_qos_db, old->pipe->p_id);
			nni_qos_db_remove_unused_msg(
			    is_sqlite, old->nano_qos_db);
#endif
			nni_qos_db_remove_all_msg(is_sqlite, old->nano_qos_db,
			    nmq_close_unack_msg_cb);
			nni_id_remove(&s->cached_sessions, p->pipe->p_id);
			log_info("cleaning session %d from cache", p->pipe->p_id);
		}
	}
#ifdef NNG_SUPP_SQLITE
	nni_qos_db_set_pipe(is_sqlite, p->pipe->nano_qos_db, p->id, clientid);
#endif
	// pipe_id is just random value of id_dyn_val with self-increment.
	nni_id_set(&s->pipes, p->id, p);
	nng_atomic_set(s->conf->lc, nni_id_count(&s->pipes));
	p->conn_param->nano_qos_db = p->pipe->nano_qos_db;
	p->nano_qos_db             = p->pipe->nano_qos_db;

	conn_param_clone(p->conn_param);
	rv = verify_connect(p->conn_param, s->conf);
	if (rv == SUCCESS) {
		if (s->conf->auth_http.enable) {
			rv = nmq_auth_http_connect(		// potential dead lock if HTTP fails
			    p->conn_param, &s->conf->auth_http);
		}
	}
	nmq_connack_encode(msg, p->conn_param, rv);
	conn_param_free(p->conn_param);
	if (rv != 0) {
		// send connack with reason code 0x05
		log_warn("Invalid auth info.");
	}

	// Dont need to manage id_map while enable SQLite.
	void *qos_db = NULL;
	if (s->conf->ext_qos_db)
		qos_db = nng_id_get(s->conf->ext_qos_db, p->pipe->p_id);
	if (qos_db != NULL && !s->conf->sqlite.enable) {
		// check sqlite compatibility
		if (p->nano_qos_db != NULL)
			nni_qos_db_fini_id_hash(p->nano_qos_db);
		p->nano_qos_db = qos_db;
		p->conn_param->nano_qos_db = qos_db;
		p->pipe->nano_qos_db = qos_db;
		nng_id_remove(s->conf->ext_qos_db, p->pipe->p_id);
	}

	// close old one (bool to prevent disconnect_ev)
	// check if pointer is different later
	if (old) {
		// check will msg delay interval
		if (conn_param_get_will_delay_timestamp(old->conn_param) >
		    nng_clock()) {
			// it is not your time yet, do not send will msg
			old->conn_param->will_flag = 0;
		}
		nni_mtx_unlock(&s->lk);
		nni_pipe_close(old->pipe);
	} else {
		nni_mtx_unlock(&s->lk);
	}
	// TODO MQTT V5 check return code
	if (rv == 0) {
		nni_sleep_aio(s->conf->qos_duration * 1500, &p->aio_timer);
	}
	nni_msg_set_cmd_type(msg, CMD_CONNACK);
	if (p->event == false) {
		// set session present in connack
		nmq_connack_session(msg, true);
	}
	nni_msg_set_conn_param(msg, p->conn_param);
	// There is no need to check the  state of aio_recv
	// Since pipe_start is definetly the first cb to be excuted of pipe.
	nni_aio_set_msg(&p->aio_recv, msg);
	// connection rate is not fast enough in this way.
	nni_aio_finish_sync(&p->aio_recv, 0, nni_msg_len(msg));
	nni_atomic_set_bool(&p->closed, false);
	return (rv);
}

// please use it within a pipe+sock lock
static inline void
close_pipe(nano_pipe *p)
{
	nano_pipe *t = NULL;
	nano_sock *s = p->broker;

	nni_atomic_set_bool(&p->closed, true);
	if (nni_list_active(&s->recvpipes, p)) {
		nni_msg *msg = nni_aio_get_msg(&p->aio_recv);
		if (msg)
			nni_msg_free(msg);
		conn_param_free(p->conn_param);
		nni_list_remove(&s->recvpipes, p);
	}
	// only remove matched pipe, could have been overwritten
	t = nni_id_get(&s->pipes, nni_pipe_id(p->pipe));
	if (t == p) {
		nni_id_remove(&s->pipes, nni_pipe_id(p->pipe));
		nng_atomic_set(s->conf->lc, nni_id_count(&s->pipes));
	}
	nano_nni_lmq_flush(&p->rlmq, false);
}

static int
nano_pipe_close(void *arg)
{
	nano_pipe *p = arg;
	nano_sock *s = p->broker;
	nano_ctx  *ctx;
	nni_aio   *aio = NULL;
	nni_msg   *msg;
	nni_pipe  *npipe        = p->pipe;
	char      *clientid     = NULL;

	log_trace(" ############## nano_pipe_close [%p] ############## ", p);
	if (npipe->cache == true) {
		// not first time we trying to close stored session pipe
		nni_atomic_swap_bool(&npipe->p_closed, false);
		return -1;
	}
	nni_mtx_lock(&s->lk);
	nni_mtx_lock(&p->lk);
	log_info("%s pipe close!", p->conn_param->clientid.body);
	// we freed the conn_param when restoring pipe
	// so check status of conn_param. just let it close silently
	if (p->conn_param->clean_start == 0) {
		// cache this pipe
		clientid = (char *) conn_param_get_clientid(p->conn_param);
	}
	if (clientid) {
		nni_pipe *new_pipe;
		if (nni_pipe_find(&new_pipe, npipe->p_id) == 0) {
			log_debug("keep session id [%s] ", p->conn_param->clientid.body);
			nni_pipe_rele(new_pipe);
			if (new_pipe == npipe) {
				log_info("A keeping Session is step down");
			} else {
				// client with session stored is kicking itself
				log_info("A keeping Session is kicked out");
			}
			log_info("session stored %d", npipe->p_id);
			nni_id_set(&s->cached_sessions, npipe->p_id, p);
			// set event to false avoid of sending the
			// disconnecting msg
			p->event     = false;
			npipe->cache = true;
			// set clean start to 1, prevent caching
			// session twice
			p->conn_param->clean_start = 1;
			nni_atomic_swap_bool(&npipe->p_closed, false);
			if (nni_list_active(&s->recvpipes, p)) {
				nni_msg *tmsg = nni_aio_get_msg(&p->aio_recv);
				if (tmsg)
					nni_msg_free(tmsg);
				conn_param_free(p->conn_param);
				nni_list_remove(&s->recvpipes, p);
			}
			nano_nni_lmq_flush(&p->rlmq, false);
			nni_mtx_unlock(&s->lk);
			nni_mtx_unlock(&p->lk);
			return -1;
		}

		// have to close & stop aio timer first, otherwise we hit null qos_db
		nni_aio_finish_error(&p->aio_timer, NNG_ECANCELED);
		nni_aio_close(&p->aio_timer);
		nni_aio_close(&p->aio_send);
		nni_aio_close(&p->aio_recv);
		// take params from npipe to new pipe
		new_pipe->packet_id = npipe->packet_id;
		// there should be no msg in this map
		if (!s->conf->sqlite.enable && new_pipe->nano_qos_db != NULL)
			nni_qos_db_fini_id_hash(new_pipe->nano_qos_db);
		new_pipe->nano_qos_db = npipe->nano_qos_db;
		npipe->nano_qos_db = NULL;

		nni_list *l        = new_pipe->subinfol;
		new_pipe->subinfol = npipe->subinfol;
		npipe->subinfol    = l;
		log_info("client kick itself while keeping session!");
	} else {
		nni_aio_close(&p->aio_send);
		nni_aio_close(&p->aio_recv);
		nni_aio_close(&p->aio_timer);
	}
	close_pipe(p);

	// TODO send disconnect msg to client if needed.
	// depends on MQTT V5 reason code
	// create disconnect event msg
	if (p->event) {
		msg = nano_msg_notify(p->conn_param, p->reason_code, 0, false);
		if (msg == NULL) {
			nni_mtx_unlock(&p->lk);
			nni_mtx_unlock(&s->lk);
			return 0;
		}
		nni_msg_set_conn_param(msg, p->conn_param);
		// clone for notification pub
		conn_param_clone(p->conn_param);
		nni_msg_set_cmd_type(msg, CMD_DISCONNECT_EV);
		nni_msg_set_pipe(msg, p->id);

		// expose disconnect event
		if ((ctx = nni_list_first(&s->recvq)) != NULL) {
			aio       = ctx->raio;
			ctx->raio = NULL;
			nni_list_remove(&s->recvq, ctx);
			nni_mtx_unlock(&p->lk);
			nni_mtx_unlock(&s->lk);
			nni_aio_set_msg(aio, msg);
			nni_aio_finish(aio, 0, nni_msg_len(msg));
			return 0;
		} else {
			// no enough ctx, so cache to waitlmq
			// free conn param when discard waitlmq
			if (nni_lmq_full(&s->waitlmq)) {
				if (nni_lmq_resize(&s->waitlmq,
				        nni_lmq_cap(&s->waitlmq) * 2) != 0) {
					log_error("wait lmq resize failed.");
					conn_param_free(p->conn_param);
					nni_msg_free(msg);
				} else
					nni_lmq_put(&s->waitlmq, msg);
			} else {
				nni_lmq_put(&s->waitlmq, msg);
			}
		}
	}
	nni_mtx_unlock(&p->lk);
	nni_mtx_unlock(&s->lk);
	return 0;
}

static void
nano_pipe_send_cb(void *arg)
{
	nano_pipe *p = arg;
	nni_msg   *msg;
	int       rv;

	log_trace("******** nano_pipe_send_cb %d ****", p->id);
	// retry here
	if ((rv = nni_aio_result(&p->aio_send)) != 0) {
		log_debug("send cb error result %d", rv);
		msg = nni_aio_get_msg(&p->aio_send);
		nni_msg_free(msg);
		nni_aio_set_msg(&p->aio_send, NULL);
		p->reason_code = rv;
		nni_pipe_close(p->pipe);
		return;
	}
	nni_mtx_lock(&p->lk);

	nni_aio_set_prov_data(&p->aio_send, 0);
	if (nni_lmq_get(&p->rlmq, &msg) == 0) {
		nni_aio_set_msg(&p->aio_send, msg);
		log_trace("rlmq msg resending! %ld msgs left\n",
		    nni_lmq_len(&p->rlmq));
		nni_pipe_send(p->pipe, &p->aio_send);
		nni_mtx_unlock(&p->lk);
		return;
	}

	p->busy = false;
	nni_mtx_unlock(&p->lk);
	return;
}

static void
nano_cancel_recv(nni_aio *aio, void *arg, int rv)
{
	nano_ctx  *ctx = arg;
	nano_sock *s   = ctx->sock;

	log_trace("*********** nano_cancel_recv ***********");
	nni_mtx_lock(&s->lk);
	if (ctx->raio == aio) {
		nni_list_remove(&s->recvq, ctx);
		ctx->raio = NULL;
		nni_aio_finish_error(aio, rv);
	}
	nni_mtx_unlock(&s->lk);
}

static void
nano_ctx_recv(void *arg, nni_aio *aio)
{
	nano_ctx  *ctx = arg;
	nano_sock *s   = ctx->sock;
	nano_pipe *p;
	// size_t     len;
	nni_msg *msg = NULL;

	if (nni_aio_begin(aio) != 0) {
		log_warn("recv aio begin failed!");
		return;
	}

	log_trace("nano_ctx_recv start %p", ctx);
	nni_mtx_lock(&s->lk);

	if (nni_lmq_get(&s->waitlmq, &msg) == 0) {
		nni_mtx_unlock(&s->lk);
		log_trace("handle msg in waitlmq.");
		nni_aio_set_msg(aio, msg);
		nni_aio_finish(aio, 0, nni_msg_len(msg));
		return;
	}

	if ((p = nni_list_first(&s->recvpipes)) == NULL) {
		int rv;
		if ((rv = nni_aio_schedule(aio, nano_cancel_recv, ctx)) != 0) {
			nni_mtx_unlock(&s->lk);
			nni_aio_finish_error(aio, rv);
			return;
		}
		if (ctx->raio != NULL) {
			// Cannot have a second receive operation pending.
			// This could be ESTATE, or we could cancel the first
			// with ECANCELED.  We elect the former.
			log_error("former aio not finish yet");
			nni_mtx_unlock(&s->lk);
			nni_aio_finish_error(aio, NNG_ESTATE);
			return;
		}
		ctx->raio = aio;
		nni_list_append(&s->recvq, ctx);
		nni_mtx_unlock(&s->lk);
		return;
	}

	msg = nni_aio_get_msg(&p->aio_recv);
	nni_aio_set_msg(&p->aio_recv, NULL);
	nni_list_remove(&s->recvpipes, p);
	nni_pipe_recv(p->pipe, &p->aio_recv);

	ctx->pipe_id = nni_pipe_id(p->pipe);
	log_trace("nano_ctx_recv ends %p pipe: %p pipe_id: %d", ctx, p,
	    ctx->pipe_id);
	nni_mtx_unlock(&s->lk);

	nni_aio_set_msg(aio, msg);
	nni_aio_finish(aio, 0, nni_msg_len(msg));
}

static void
nano_pipe_recv_cb(void *arg)
{
	uint32_t    len;
	uint16_t    ackid;
	uint8_t     type, len_of_varint = 0;
	uint8_t    *ptr;
	nano_pipe  *p      = arg;
	nano_sock  *s      = p->broker;
	conn_param *cparam = NULL;
	nano_ctx   *ctx;
	nni_msg    *msg, *qos_msg = NULL;
	nni_aio    *aio;
	nni_pipe   *npipe = p->pipe;
	int         rv = 0;

	bool is_sqlite = s->conf->sqlite.enable;

	if ((rv = nni_aio_result(&p->aio_recv)) != 0) {
		// unexpected disconnect, dont mind the TSAN here
		p->reason_code = rv;
		nni_pipe_close(p->pipe);
		return;
	}
	log_trace(" ######### nano_pipe_recv_cb ######### ");
	p->ka_refresh = 0;
	msg           = nni_aio_get_msg(&p->aio_recv);
	if (msg == NULL) {
		goto end;
	}
	if (nni_atomic_get_bool(&p->closed)) {
		nni_msg_free(msg);
		return;
	}

	// ttl = nni_atomic_get(&s->ttl);
	nni_msg_set_pipe(msg, p->id);
	ptr    = nni_msg_body(msg);
	cparam = p->conn_param;
	type = nng_msg_cmd_type(msg);
	switch (type) {
	case CMD_SUBSCRIBE:
		// Fall through
	case CMD_UNSUBSCRIBE:
		// 1. Clone for App layer 2. Clone should be called before being used
		conn_param_clone(cparam);
		if (cparam->pro_ver == MQTT_PROTOCOL_VERSION_v5) {
			len = get_var_integer(ptr + 2, &len_of_varint);
			nni_msg_set_payload_ptr(
			    msg, ptr + 2 + len + len_of_varint);
		} else {
			nni_msg_set_payload_ptr(msg, ptr + 2);
		}
		break;
	case CMD_DISCONNECT:
		if (p->conn_param) {
			p->conn_param->will_flag = 0;
		} else {
			nni_pipe_close(p->pipe);
			break;
		}
		if (p->conn_param->pro_ver == MQTT_VERSION_V5) {
			rv |= nni_mqtt_msg_proto_data_alloc(msg);
			rv |= nni_mqttv5_msg_decode(msg);
			if (rv == 0) {
				nni_mqtt_proto_data *mqtt =
				    nni_msg_get_proto_data(msg);
				property *prop =
				    mqtt->var_header.disconnect.properties;
				property_data *prop_data = property_get_value(
				    prop, SESSION_EXPIRY_INTERVAL);
				if (prop_data) {
					p->conn_param
					    ->session_expiry_interval =
					    prop_data->p_value.u32;
				}
			} else {
				p->reason_code = MQTT_ERR_MALFORMED;
			}
		} else {
			p->reason_code = 0x00;
		}
		nni_pipe_close(p->pipe);
		break;
	case CMD_CONNACK:
	case CMD_PUBLISH:
		// 1. Clone for App layer 2. Clone should be called before being used
		conn_param_clone(cparam);
		break;
	case CMD_PUBACK:
	case CMD_PUBCOMP:
		nni_mtx_lock(&p->lk);
		NNI_GET16(ptr, ackid);
		p->rid = ackid + 1;
		if ((qos_msg = nni_qos_db_get(is_sqlite, npipe->nano_qos_db,
		         npipe->p_id, ackid)) != NULL) {
			nni_qos_db_remove_msg(
			    is_sqlite, npipe->nano_qos_db, qos_msg);
			nni_qos_db_remove(
			    is_sqlite, npipe->nano_qos_db, npipe->p_id, ackid);
		} else {
			log_warn("ACK failed! qos msg %ld not found!", ackid);
		}
		nni_mtx_unlock(&p->lk);
	case CMD_CONNECT:
	case CMD_PUBREC:
	case CMD_PUBREL:
		goto drop;
	case CMD_PINGREQ:
		nni_mtx_lock(&p->lk);
		nni_msg_clone(s->pingmsg);
		if (!p->busy) {
			p->busy = true;
			nni_aio_set_msg(&p->aio_send, s->pingmsg);
			nni_pipe_send(p->pipe, &p->aio_send);
		} else {
			if (nni_lmq_put(&p->rlmq, s->pingmsg) != 0) {
				nni_msg_free(s->pingmsg);
			}
		}
		nni_mtx_unlock(&p->lk);
		goto drop;
	default:
		goto drop;
	}
	nni_mtx_lock(&s->lk);
	if (nni_atomic_get_bool(&p->closed)) {
		// If we are closed, then we can't return data.
		// This drops DISCONNECT packet.
		nni_aio_set_msg(&p->aio_recv, NULL);
		nni_msg_free(msg);
		if (type == CMD_SUBSCRIBE || type == CMD_UNSUBSCRIBE ||
		    type == CMD_CONNACK || type == CMD_PUBLISH)
			conn_param_free(cparam);
		log_trace("pipe is closed abruptly!");
		nni_mtx_unlock(&s->lk);
		return;
	}

	if ((ctx = nni_list_first(&s->recvq)) == NULL) {
		// No one waiting to receive yet, holding pattern.
		// Dont use waitlmq cache, cause back-pressure.
		if (!nni_list_active(&s->recvpipes, p))
			nni_list_append(&s->recvpipes, p);
		else
			log_error("Unscheduled receving!");
		nni_mtx_unlock(&s->lk);
		// this gonna cause broker lagging, so we reduce outputs 1 in 256
		rotate ++;
		if (rotate >>8 & 0x01) {
			log_warn("no ctx found!! create more ctxs!");
			rotate  = rotate >> 8 ;
		}
		return;
	}

	nni_list_remove(&s->recvq, ctx);
	aio       = ctx->raio;
	ctx->raio = NULL;
	nni_aio_set_msg(&p->aio_recv, NULL);

	ctx->pipe_id = p->id;
	log_trace("currently processing pipe_id: %d", p->id);
	nni_mtx_unlock(&s->lk);

	// schedule another receive
	nni_pipe_recv(p->pipe, &p->aio_recv);
	nni_aio_set_msg(aio, msg);
	nni_aio_finish_sync(aio, 0, nni_msg_len(msg));
	log_trace("end of nano_pipe_recv_cb %p", ctx);
	return;

drop:
	nni_msg_free(msg);
end:
	nni_aio_set_msg(&p->aio_recv, NULL);
	nni_pipe_recv(p->pipe, &p->aio_recv);
	return;
}

static int
nano_sock_set_max_ttl(void *arg, const void *buf, size_t sz, nni_opt_type t)
{
	nano_sock *s = arg;
	int        ttl;
	int        rv;

	if ((rv = nni_copyin_int(&ttl, buf, sz, 1, NNI_MAX_MAX_TTL, t)) == 0) {
		nni_atomic_set(&s->ttl, ttl);
	}
	return (rv);
}

static int
nano_sock_get_max_ttl(void *arg, void *buf, size_t *szp, nni_opt_type t)
{
	nano_sock *s = arg;

	return (nni_copyout_int(nni_atomic_get(&s->ttl), buf, szp, t));
}

static int
nano_sock_get_idmap(void *arg, void *buf, size_t *szp, nni_opt_type t)
{
	nano_sock *s = arg;

	return (nni_copyout_ptr(&s->pipes, buf, szp, t));
}

#if defined(NNG_SUPP_SQLITE)
static int
nano_sock_get_qos_db(void *arg, void *buf, size_t *szp, nni_opt_type t)
{
	nano_sock *s = arg;

	return (nni_copyout_ptr(s->sqlite_db, buf, szp, t));
}
#endif

static int
nano_sock_get_sendfd(void *arg, void *buf, size_t *szp, nni_opt_type t)
{
	nano_sock *s = arg;
	int        rv;
	int        fd;

	if ((rv = nni_pollable_getfd(&s->writable, &fd)) != 0) {
		return (rv);
	}
	return (nni_copyout_int(fd, buf, szp, t));
}

static int
nano_sock_get_recvfd(void *arg, void *buf, size_t *szp, nni_opt_type t)
{
	nano_sock *s = arg;
	int        rv;
	int        fd;

	if ((rv = nni_pollable_getfd(&s->readable, &fd)) != 0) {
		return (rv);
	}

	return (nni_copyout_int(fd, buf, szp, t));
}

static void
nano_sock_send(void *arg, nni_aio *aio)
{
	nano_sock *s = arg;

	nano_ctx_send(&s->ctx, aio);
}

static void
nano_sock_recv(void *arg, nni_aio *aio)
{
	nano_sock *s = arg;

	nano_ctx_recv(&s->ctx, aio);
}

static void
nano_sock_setdb(void *arg, void *data)
{
	nano_sock *s         = arg;
	conf *     nano_conf = data;

	s->conf = nano_conf;
	s->db   = nano_conf->db_root;

#ifdef NNG_SUPP_SQLITE
	if (s->conf->sqlite.enable) {
		nni_qos_db_init_sqlite(s->sqlite_db,
		    s->conf->sqlite.mounted_file_path, DB_NAME, true);
		nni_qos_db_reset_pipe(s->conf->sqlite.enable, s->sqlite_db);
	}
#endif

	// check pre-configured session id
	// or switch to lmq for better msg rate?
	for (size_t y = 0; y < nano_conf->pre_sessions.count; y++) {
		conf_session_node *node = nano_conf->pre_sessions.nodes[y];
		uint32_t hashn = DJBHashn(node->clientid, strlen(node->clientid));
		node->idhash = hashn;
		if (!nano_conf->sqlite.enable) {
			void *qos_db;
			nni_qos_db_init_id_hash(qos_db);
			nng_id_set(nano_conf->ext_qos_db, hashn, qos_db);
		} else {
#ifdef NNG_SUPP_SQLITE
			nni_qos_db_set_pipe(s->conf->sqlite.enable,
				s->sqlite_db, hashn, node->clientid);
#endif
			log_warn("Not recommend to use Preset session with SQLite ");
		}
	}

}

// This is the global protocol structure -- our linkage to the core.
// This should be the only global non-static symbol in this file.
static nni_proto_pipe_ops nano_pipe_ops = {
	.pipe_size  = sizeof(nano_pipe),
	.pipe_init  = nano_pipe_init,
	.pipe_fini  = nano_pipe_fini,
	.pipe_start = nano_pipe_start,
	.pipe_close = nano_pipe_close,
	.pipe_stop  = nano_pipe_stop,
};

static nni_proto_ctx_ops nano_ctx_ops = {
	.ctx_size = sizeof(nano_ctx),
	.ctx_init = nano_ctx_init,
	.ctx_fini = nano_ctx_fini,
	.ctx_send = nano_ctx_send,
	.ctx_recv = nano_ctx_recv,
};

static nni_option nano_sock_options[] = {
	{
	    .o_name = NNG_OPT_MAXTTL,
	    .o_get  = nano_sock_get_max_ttl,
	    .o_set  = nano_sock_set_max_ttl,
	},
	{
	    .o_name = NNG_OPT_RECVFD,
	    .o_get  = nano_sock_get_recvfd,
	},
	{
	    .o_name = NNG_OPT_SENDFD,
	    .o_get  = nano_sock_get_sendfd,
	},
	{
	    .o_name = NMQ_OPT_MQTT_PIPES,
	    .o_get  = nano_sock_get_idmap,
	},
#if defined(NNG_SUPP_SQLITE)
	{
	    .o_name = NMQ_OPT_MQTT_QOS_DB,
	    .o_get  = nano_sock_get_qos_db,
	},
#endif
	{
	    .o_name = NULL,
	},
};

static nni_proto_sock_ops nano_sock_ops = {
	.sock_size    = sizeof(nano_sock),
	.sock_init    = nano_sock_init,
	.sock_fini    = nano_sock_fini,
	.sock_open    = nano_sock_open,
	.sock_close   = nano_sock_close,
	.sock_options = nano_sock_options,
	.sock_send    = nano_sock_send,
	.sock_recv    = nano_sock_recv,
};

static nni_proto nano_tcp_proto = {
	.proto_version  = NNI_PROTOCOL_VERSION,
	.proto_self     = { NNG_NMQ_TCP_SELF, NNG_NMQ_TCP_SELF_NAME },
	.proto_peer     = { NNG_NMQ_TCP_PEER, NNG_NMQ_TCP_PEER_NAME },
	.proto_flags    = NNI_PROTO_FLAG_SNDRCV,
	.proto_sock_ops = &nano_sock_ops,
	.proto_pipe_ops = &nano_pipe_ops,
	.proto_ctx_ops  = &nano_ctx_ops,
};

int
nng_nmq_tcp0_open(nng_socket *sidp)
{
	log_debug("open up nmq tcp0 protocol.");
	return (nni_proto_mqtt_open(sidp, &nano_tcp_proto, nano_sock_setdb));
}
