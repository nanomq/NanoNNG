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

typedef struct exchange_sock_s         exchange_sock_t;
typedef struct exchange_node_s         exchange_node_t;
typedef struct exchange_sendmessages_s exchange_sendmessages_t;

struct exchange_sendmessages_s {
	int   *key;
	nni_msg *msg;
	nni_list_node node;
};

struct exchange_node_s {
	exchange_t      *ex;
	exchange_sock_t *sock;
	nni_aio         saio;
	nni_list        send_messages;
	unsigned int    send_messages_num;
	bool            isBusy;
	nni_mtx         mtx;
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

/* lock ex_node before enqueue */
static int
exchange_node_send_messages_enqueue(exchange_node_t *ex_node, int *key, nni_msg *msg)
{
	exchange_sendmessages_t *send_msg = NULL;
	// isnt a lmq more approriate?
	send_msg = (exchange_sendmessages_t *)nng_alloc(sizeof(exchange_sendmessages_t));
	if (send_msg == NULL) {
		/* free key and msg here! */
		nni_msg_free(msg);
		nng_free(key, sizeof(int));
		log_error("exchange_sendmessages_enqueue failed! No memory!\n");
		return -1;
	}

	send_msg->key = key;
	send_msg->msg = msg;

	NNI_LIST_NODE_INIT(&send_msg->node);

	if (ex_node->send_messages_num >= 1024) {
		log_error("exchange_sendmessages_enqueue failed! send_messages_num >= 1024!\n");
		/* free key and msg here! */
		nni_msg_free(send_msg->msg);
		nng_free(send_msg->key, sizeof(int));
		nng_free(send_msg, sizeof(*send_msg));
		return -1;
	}
	nni_list_append(&ex_node->send_messages, send_msg);
	ex_node->send_messages_num++;

	return 0;
}

/* lock ex_node before dequeue */
static int
exchange_node_send_messages_dequeue(exchange_node_t *ex_node, int **key, nni_msg **msg)
{
	exchange_sendmessages_t *send_msg = NULL;
	// nni_list can only be used inside the protocol layer
	send_msg = nni_list_first(&ex_node->send_messages);
	if (send_msg == NULL) {
		return -1;
	}

	*key = send_msg->key;
	*msg = send_msg->msg;
	nni_list_remove(&ex_node->send_messages, send_msg);
	ex_node->send_messages_num--;

	return 0;
}

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
	node->send_messages_num = 0;

	nni_aio_init(&node->saio, exchange_send_cb, node);
	NNI_LIST_INIT(&node->send_messages, exchange_sendmessages_t, node);
	nni_mtx_init(&node->mtx);

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
	exchange_sock_t *s = arg;
	exchange_node_t *ex_node;
	exchange_sendmessages_t *send_message;

	ex_node = s->ex_node;
	while (!nni_list_empty(&ex_node->send_messages)) {
		send_message = nni_list_last(&ex_node->send_messages);
		if (send_message) {
			nni_list_remove(&ex_node->send_messages, send_message);
			/* free key and msg here! */
			nni_msg_free(send_message->msg);
			nng_free(send_message->key, sizeof(int));
			nng_free(send_message, sizeof(*send_message));
			send_message = NULL;
		}
	}

	nni_aio_fini(&ex_node->saio);
	nni_mtx_fini(&ex_node->mtx);
	exchange_release(ex_node->ex);

	nng_free(ex_node, sizeof(*ex_node));
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
inline static int
exchange_client_handle_msg(exchange_node_t *ex_node, int *key, nni_msg *msg)
{
	int ret = 0;
	nni_msg *tmsg = NULL;

	tmsg = nni_id_get(&ex_node->sock->rbmsgmap, *key);
	if (tmsg != NULL) {
		log_error("msg already in rbmsgmap\n");
		/* free key and msg here! */
		nni_msg_free(msg);
		nng_free(key, sizeof(int));
		return -1;
	}

	(void)exchange_handle_msg(ex_node->ex, *key, msg);

	ret = nni_id_set(&ex_node->sock->rbmsgmap, *key, msg);
	if (ret != 0) {
		log_error("rbmsgmap set failed\n");
		/* free key and msg here! */
		nni_msg_free(msg);
		nng_free(key, sizeof(int));
		return -1;
	}
	/* free key here! */
	nng_free(key, sizeof(int));

	return 0;
}

static void
exchange_sock_send(void *arg, nni_aio *aio)
{
	nni_msg *        msg  = NULL;
	exchange_node_t *ex_node;
	exchange_sock_t *s = arg;

	msg = nni_aio_get_msg(aio);
	if (msg == NULL) {
		nni_aio_finish(aio, 0, 0);
		return;
	}

	if (nni_msg_get_type(msg) != CMD_PUBLISH) {
		nni_aio_finish(aio, 0, 0);
		return;
	}

	nni_mtx_lock(&s->mtx);
	if (s->ex_node == NULL) {
		nni_aio_finish(aio, 0, 0);
		nni_mtx_unlock(&s->mtx);
		return;
	}

	int *key = nni_aio_get_prov_data(aio);
	if (key == NULL) {
		log_error("key is NULL\n");
		nni_aio_finish(aio, 0, 0);
		nni_mtx_unlock(&s->mtx);
		return;
	}

	ex_node = s->ex_node;
	nni_mtx_lock(&ex_node->mtx);
	if (!ex_node->isBusy) {
		// FIX here
		if (nni_aio_begin(&ex_node->saio) != 0) {
			nni_mtx_unlock(&ex_node->mtx);
			nni_aio_finish(aio, 0, 0);
			nni_mtx_unlock(&s->mtx);
			return;
		}
		ex_node->isBusy = true;

		(void)exchange_client_handle_msg(ex_node, key, msg);
		nni_mtx_unlock(&ex_node->mtx);
		nni_aio_finish(&ex_node->saio, 0, 0);
	} else {
		if (exchange_node_send_messages_enqueue(ex_node, key, msg) != 0) {
			log_error("exchange_node_send_messages_enqueue failed!\n");
		}
		nni_mtx_unlock(&ex_node->mtx);
	}
	nni_aio_finish(aio, 0, 0);
	nni_mtx_unlock(&s->mtx);
	return;
}

static void
exchange_send_cb(void *arg)
{
	exchange_node_t *ex_node = arg;
	nni_msg         *msg = NULL;
	int             *key = NULL;

	if (ex_node == NULL) {
		return;
	}

	exchange_sock_t *s = ex_node->sock;
	if (nni_atomic_get_bool(&s->closed)) {
		// This occurs if the mqtt_pipe_close has been called.
		// In that case we don't want any more processing.
		return;
	}

	nni_mtx_lock(&ex_node->mtx);
	if (nni_aio_result(&ex_node->saio) != 0) {
		nni_mtx_unlock(&ex_node->mtx);
		return;
	}
	if (exchange_node_send_messages_dequeue(ex_node, &key, &msg) == 0) {
		(void)exchange_client_handle_msg(ex_node, key, msg);
		nni_mtx_unlock(&ex_node->mtx);
		nni_aio_finish(&ex_node->saio, 0, 0);
		return;
	}

	ex_node->isBusy = false;
	nni_mtx_unlock(&ex_node->mtx);

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

// int
// exchange_client_get_msgs_by_key(void *arg, uint32_t key, uint32_t count, nni_list **list)
// {
// 	int ret = 0;
// 	int topic_len = 0;
// 	exchange_sock_t *s = arg;
// 	nni_msg *tmsg = NULL;
// 	nni_id_map *rbmsgmap = &s->rbmsgmap;

// 	nni_mtx_lock(&s->mtx);
// 	tmsg = nni_id_get(rbmsgmap, key);
// 	if (tmsg == NULL) {
// 		nni_mtx_unlock(&s->mtx);
// 		return -1;
// 	}

// 	exchange_node_t *ex_node = NULL;
// 	NNI_LIST_FOREACH (&s->ex_queue, ex_node) {
// 		nni_mtx_lock(&ex_node->mtx);
// 		if (strncmp(nng_mqtt_msg_get_publish_topic(tmsg, &topic_len),
// 					ex_node->ex->topic, strlen(ex_node->ex->topic)) != 0) {
// 			nni_mtx_unlock(&ex_node->mtx);
// 			continue;
// 		} else {
// 			/* Only one exchange with one ringBuffer now */
// 			ret = ringBuffer_search_msgs_by_key(ex_node->ex->rbs[0], key, count, list);
// 			if (ret != 0 || list == NULL) {
// 				log_error("ringBuffer_get_msgs_by_key failed!\n");
// 				nni_mtx_unlock(&ex_node->mtx);
// 				nni_mtx_unlock(&s->mtx);
// 				return -1;
// 			}
// 			nni_mtx_unlock(&ex_node->mtx);
// 			nni_mtx_unlock(&s->mtx);
// 			return 0;
// 		}
// 	}

// 	nni_mtx_unlock(&s->mtx);
// 	return ret;
// }

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
