// Copyright 2023 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
#include "core/nng_impl.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/exchange/exchange_client.h"
#include "nng/exchange/exchange.h"
#include "supplemental/mqtt/mqtt_msg.h"

#define NANO_MAX_MQ_BUFFER_LEN 1024

typedef struct exchange_sock_s         exchange_sock_t;
typedef struct exchange_node_s         exchange_node_t;

struct exchange_node_s {
	exchange_t      *ex;
	exchange_sock_t *sock;
	nni_aio         saio;
	bool            isBusy;
	nni_mtx         mtx;
	nni_lmq         send_messages;
};

struct exchange_sock_s {
	nni_mtx         mtx;
	nni_atomic_bool closed;
	nni_id_map      rbmsgmap;
	exchange_node_t *ex_node;
};


static void exchange_sock_init(void *arg, nni_sock *sock);
static void exchange_sock_fini(void *arg);
static void exchange_sock_open(void *arg);
static void exchange_sock_send(void *arg, nni_aio *aio);
static void exchange_send_cb(void *arg);

static int
exchange_add_ex(exchange_sock_t *s, exchange_t *ex)
{
	nni_mtx_lock(&s->mtx);

	if (s->ex_node != NULL) {
		log_error("exchange client add exchange failed! ex_node is not NULL!\n");
		nni_mtx_unlock(&s->mtx);
		return -1;
	}

	exchange_node_t *node;
	node = (exchange_node_t *)nng_alloc(sizeof(exchange_node_t));
	if (node == NULL) {
		log_error("exchange client add exchange failed! No memory!\n");
		nni_mtx_unlock(&s->mtx);
		return -1;
	}

	node->isBusy = false;
	node->ex = ex;
	node->sock = s;

	nni_aio_init(&node->saio, exchange_send_cb, node);
	nni_mtx_init(&node->mtx);
	nni_lmq_init(&node->send_messages, NANO_MAX_MQ_BUFFER_LEN);

	s->ex_node = node;
	nni_mtx_unlock(&s->mtx);
	return 0;
}

static void
exchange_sock_init(void *arg, nni_sock *sock)
{
	NNI_ARG_UNUSED(sock);
	exchange_sock_t *s = arg;

	nni_atomic_init_bool(&s->closed);
	nni_atomic_set_bool(&s->closed, false);
	nni_mtx_init(&s->mtx);
	nni_id_map_init(&s->rbmsgmap, 0, 0, true);

	return;
}

static void
exchange_sock_fini(void *arg)
{
	nni_msg *msg;
	nni_aio *aio;
	exchange_sock_t *s = arg;
	exchange_node_t *ex_node;

	ex_node = s->ex_node;
	while (nni_lmq_get(&ex_node->send_messages, &msg) == 0) {
		aio = nni_msg_get_proto_data(msg);
		if (aio != NULL) {
			nni_aio_finish_error(aio, NNG_ECLOSED);
		}

		nni_msg_free(msg);
	}

	nni_aio_fini(&ex_node->saio);
	nni_mtx_fini(&ex_node->mtx);
	nni_lmq_fini(&ex_node->send_messages);
	exchange_release(ex_node->ex);

	nni_free(ex_node, sizeof(*ex_node));
	ex_node = NULL;

	nni_id_map_fini(&s->rbmsgmap);
	nni_mtx_fini(&s->mtx);

	return;
}

static void
exchange_sock_open(void *arg)
{
	NNI_ARG_UNUSED(arg);
	return;
}

static void
exchange_sock_close(void *arg)
{
	exchange_sock_t *s = arg;
	exchange_node_t *ex_node = s->ex_node;

	nni_atomic_set_bool(&s->closed, true);
	nni_aio_close(&ex_node->saio);

	return;
}

/* Check if the msg is already in rbmsgmap, if not, add it to rbmsgmap */
static inline int
exchange_client_handle_msg(exchange_node_t *ex_node, nni_msg *msg, nni_aio *aio)
{
	int ret = 0;
	uint32_t key;
	nni_msg *tmsg = NULL;

	key = (uintptr_t)nni_aio_get_prov_data(aio);
	nni_aio_set_prov_data(aio, NULL);

	tmsg = nni_id_get(&ex_node->sock->rbmsgmap, key);
	if (tmsg != NULL) {
		log_error("msg already in rbmsgmap, overwirte is not allowed");
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}

	ret = nni_id_set(&ex_node->sock->rbmsgmap, key, msg);
	if (ret != 0) {
		log_error("rbmsgmap set failed");
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}

	ret = exchange_handle_msg(ex_node->ex, key, msg, aio);
	if (ret != 0) {
		log_error("exchange_handle_msg failed!\n");
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}
	nng_msg **msgs = nng_aio_get_prov_data(aio);
	if (msgs != NULL) {
		/* Clean up rbmsgmap */
		nng_msg *tmsg = nng_aio_get_msg(aio);
		int *msgs_lenp = (int *)nng_msg_get_proto_data(tmsg);
		if (msgs_lenp != NULL) {
			for (int i = 0; i < *msgs_lenp; i++) {
				if (msgs[i] != NULL) {
					uint32_t tkey = (uintptr_t)nng_msg_get_proto_data(msgs[i]);
					nni_id_remove(&ex_node->sock->rbmsgmap, tkey);
				}
			}
		}
	}

	return 0;
}

static void
exchange_sock_send(void *arg, nni_aio *aio)
{
	nni_msg         *msg = NULL;
	exchange_node_t *ex_node = NULL;
	exchange_sock_t *s = arg;

	if (nni_aio_begin(aio) != 0) {
		log_error("reuse aio in exchanging!");
		return;
	}

	msg = nni_aio_get_msg(aio);
	nni_aio_set_msg(aio, NULL);
	if (msg == NULL) {
		nni_aio_finish_error(aio, NNG_EINVAL);
		return;
	}
	if (nni_msg_get_type(msg) != CMD_PUBLISH) {
		nni_aio_finish_error(aio, NNG_EINVAL);
		return;
	}
	nni_mtx_lock(&s->mtx);
	if (s->ex_node == NULL) {
		nni_aio_finish_error(aio, NNG_EINVAL);
		nni_mtx_unlock(&s->mtx);
		return;
	}

	ex_node = s->ex_node;
	nni_mtx_lock(&ex_node->mtx);  // Too complex lock, performance lost
	/* Store aio in msg proto data */
    nni_msg_set_proto_data(msg, NULL, (void *)aio);
	if (!ex_node->isBusy) {
		ex_node->isBusy = true;
		nni_aio_set_msg(&ex_node->saio, msg);
		nni_mtx_unlock(&ex_node->mtx);
		// kick off
		nni_aio_finish(&ex_node->saio, 0, nni_msg_len(msg));
	} else {
		if (nni_lmq_put(&ex_node->send_messages, msg) != 0) {
			log_error("nni_lmq_put failed! msg lost\n");
			nni_msg_free(msg);
		}
		nni_mtx_unlock(&ex_node->mtx);
		/* don't finish user aio here, finish user aio in send_cb */
	}
	nni_mtx_unlock(&s->mtx);
	return;
}

static void
exchange_send_cb(void *arg)
{
	exchange_node_t *ex_node = arg;
	nni_msg         *msg = NULL;
	nni_aio         *user_aio = NULL;
	int             ret = 0;

	if (ex_node == NULL) {
		return;
	}

	exchange_sock_t *s = ex_node->sock;
	if (nni_atomic_get_bool(&s->closed)) {
		// This occurs if the mqtt_pipe_close has been called.
		// In that case we don't want any more processing.
		return;
	}

	if (nni_aio_result(&ex_node->saio) != 0) {
		nni_mtx_unlock(&ex_node->mtx);
		return;
	}

	nni_mtx_lock(&ex_node->mtx);
	// send cached msg first
	while (nni_lmq_get(&ex_node->send_messages, &msg) == 0) {
		user_aio = (nni_aio *) nni_msg_get_proto_data(msg);
		if (user_aio == NULL) {
			log_error("user_aio is NULL\n");
			break;
		}
		// make sure msg is in order
		ret = exchange_client_handle_msg(ex_node, msg, user_aio);
		log_info("enqueue in lmq");
		if (ret != 0) {
			log_error(
			    "exchange_client_handle cached msg failed!\n");
			nni_aio_finish_error(user_aio, NNG_EINVAL);
		} else {
			nni_aio_finish(user_aio, 0, 0);
		}
	}
	// check msg in aio & send
	if ((msg = nni_aio_get_msg(&ex_node->saio)) != NULL) {
		user_aio = (nni_aio *) nni_msg_get_proto_data(msg);
		nni_aio_set_msg(&ex_node->saio, NULL);
		ret = exchange_client_handle_msg(ex_node, msg, user_aio);
		log_info("enqueue in cb");
		if (ret != 0) {
			log_error("exchange_client_handle_msg failed!\n");
			nni_aio_finish_error(user_aio, NNG_EINVAL);
		} else {
			nni_aio_finish(user_aio, 0, 0);
		}
	}
	nni_mtx_unlock(&ex_node->mtx);
	ex_node->isBusy = false;
	return;
}

static nni_proto_pipe_ops exchange_pipe_ops = {
	.pipe_size  = 0,
	.pipe_init  = NULL,
	.pipe_fini  = NULL,
	.pipe_start = NULL,
	.pipe_close = NULL,
	.pipe_stop  = NULL,
};

static int
exchange_sock_bind_exchange(void *arg, const void *v, size_t sz, nni_opt_type t)
{
	exchange_sock_t *s = arg;
	int rv;

	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);

	exchange_t *ex = (exchange_t *)(*(void **)v);
	rv = exchange_add_ex(s, ex);

	return (rv);
}

static int
exchange_sock_get_rbmsgmap(void *arg, void *v, size_t *szp, nni_opt_type t)
{
	exchange_sock_t *s = arg;
	int              rv;

	nni_mtx_lock(&s->mtx);
	rv = nni_copyout_ptr(&s->rbmsgmap, v, szp, t);
	nni_mtx_unlock(&s->mtx);
	return (rv);
}

int
exchange_client_get_msg_by_key(void *arg, uint32_t key, nni_msg **msg)
{
	exchange_sock_t *s = arg;
	nni_id_map *rbmsgmap = &s->rbmsgmap;

	if (msg == NULL) {
		return -1;
	}

	nni_msg *tmsg = NULL;
	tmsg = nni_id_get(rbmsgmap, key);
	if (tmsg == NULL) {
		return -1;
	}

	*msg = tmsg;
	return 0;
}

int
exchange_client_get_msgs_by_key(void *arg, uint32_t key, uint32_t count, nng_msg ***list)
{
	int ret = 0;
	exchange_sock_t *s = arg;
	nni_msg *tmsg = NULL;
	nni_id_map *rbmsgmap = &s->rbmsgmap;

	nni_mtx_lock(&s->mtx);

	tmsg = nni_id_get(rbmsgmap, key);
	if (tmsg == NULL || list == NULL) {
		nni_mtx_unlock(&s->mtx);
		return -1;
	}

	exchange_node_t *ex_node = s->ex_node;
	nni_mtx_lock(&ex_node->mtx);
	/* Only one exchange with one ringBuffer now */
	ret = ringBuffer_search_msgs_by_key(ex_node->ex->rbs[0], key, count, list);
	if (ret != 0 || *list == NULL) {
		log_error("ringBuffer_get_msgs_by_key failed!\n");
		nni_mtx_unlock(&ex_node->mtx);
		nni_mtx_unlock(&s->mtx);
		return -1;
	}

	nni_mtx_unlock(&ex_node->mtx);
	nni_mtx_unlock(&s->mtx);
	return 0;
}

static nni_option exchange_sock_options[] = {
	{
	    .o_name = NNG_OPT_EXCHANGE_BIND,
	    .o_set  = exchange_sock_bind_exchange,
	},
	{
		.o_name = NNG_OPT_EXCHANGE_GET_RBMSGMAP,
		.o_get  = exchange_sock_get_rbmsgmap,
	},
	{
	    .o_name = NULL,
	},
};
static nni_proto_ctx_ops exchange_ctx_ops = {
	.ctx_size    = 0,
	.ctx_init    = NULL,
	.ctx_fini    = NULL,
	.ctx_recv    = NULL,
	.ctx_send    = NULL,
	.ctx_options = NULL,
};

static nni_proto_sock_ops exchange_sock_ops = {
	.sock_size    = sizeof(exchange_sock_t),
	.sock_init    = exchange_sock_init,
	.sock_fini    = exchange_sock_fini,
	.sock_open    = exchange_sock_open,
	.sock_close   = exchange_sock_close,
	.sock_options = exchange_sock_options,
	.sock_send    = exchange_sock_send,
	.sock_recv    = NULL,
};

static nni_proto exchange_proto = {
	.proto_version  = NNI_PROTOCOL_VERSION,
	.proto_self     = { NNG_EXCHANGE_SELF, NNG_EXCHANGE_SELF_NAME },
	.proto_peer     = { NNG_EXCHANGE_PEER, NNG_EXCHANGE_PEER_NAME },
	/* TODO: send only */
	.proto_flags    = NNI_PROTO_FLAG_SNDRCV,
	.proto_sock_ops = &exchange_sock_ops,
	.proto_pipe_ops = &exchange_pipe_ops,
	.proto_ctx_ops  = &exchange_ctx_ops,
};

int
nng_exchange_client_open(nng_socket *sock)
{
	return (nni_proto_open(sock, &exchange_proto));
}
