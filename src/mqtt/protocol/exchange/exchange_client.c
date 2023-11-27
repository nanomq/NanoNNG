// Copyright 2023 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "core/nng_impl.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "supplemental/mqtt/mqtt_msg.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/exchange/exchange_client.h"

static void exchange_sock_init(void *arg, nni_sock *sock);
static void exchange_sock_fini(void *arg);
static void exchange_sock_open(void *arg);
static void exchange_sock_send(void *arg, nni_aio *aio);
static void exchange_send_cb(void *arg);

static int
exchange_add_ex(exchange_sock_t *s, exchange_t *ex)
{
	nni_mtx_lock(&s->mtx);
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
	nni_lmq_init(&node->send_messages, 1024);

	NNI_LIST_NODE_INIT(&node->exnode);
	nni_list_append(&s->ex_queue, node);

	nni_mtx_init(&node->mtx);

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
	NNI_LIST_INIT(&s->ex_queue, exchange_node_t, exnode);

	return;
}

static void
exchange_sock_fini(void *arg)
{
	exchange_sock_t *s = arg;
	exchange_node_t *ex_node;

	NNI_LIST_FOREACH (&s->ex_queue, ex_node) {
		nni_aio_fini(&ex_node->saio);
		nni_lmq_fini(&ex_node->send_messages);
		nni_mtx_fini(&ex_node->mtx);
		exchange_release(ex_node->ex);
	}

	ex_node = NULL;
	while (!nni_list_empty(&s->ex_queue)) {
		ex_node = nni_list_last(&s->ex_queue);
		if (ex_node) {
			nni_list_remove(&s->ex_queue, ex_node);
			nng_free(ex_node, sizeof(*ex_node));
			ex_node = NULL;
		}
	}

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
	exchange_node_t *ex_node;

	nni_atomic_set_bool(&s->closed, true);
	NNI_LIST_FOREACH (&s->ex_queue, ex_node) {
		nni_aio_close(&ex_node->saio);
	}

	return;
}

static void
exchange_sock_send(void *arg, nni_aio *aio)
{
	nni_msg *        msg  = NULL;
	nni_msg *        tmsg  = NULL;
	exchange_node_t *ex_node;
	exchange_sock_t *s = arg;
	uint32_t topic_len = 0;

	msg = nni_aio_get_msg(aio);
	if (msg == NULL) {
		nni_aio_finish(aio, 0, 0);
		return;
	}

	nni_mqtt_packet_type ptype = nni_mqtt_msg_get_packet_type(msg);
	if (ptype != NNG_MQTT_PUBLISH) {
		nni_aio_finish(aio, 0, 0);
		return;
	}

	/* TODO: Remove this lock? */
	nni_mtx_lock(&s->mtx);
	if (nni_list_empty(&s->ex_queue)) {
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish(aio, 0, 0);
		return;
	}
	nni_mtx_unlock(&s->mtx);

	NNI_LIST_FOREACH (&s->ex_queue, ex_node) {
		nni_mtx_lock(&ex_node->mtx);
		if (strncmp(nng_mqtt_msg_get_publish_topic(msg, &topic_len),
					ex_node->ex->topic, strlen(ex_node->ex->topic)) != 0) {
			continue;
		}

		if (!ex_node->isBusy) {
			if (nni_aio_begin(&ex_node->saio) != 0) {
				nni_mtx_unlock(&ex_node->mtx);
				nni_aio_finish(aio, 0, 0);
				return;
			}
			ex_node->isBusy = true;
			nni_mtx_unlock(&ex_node->mtx);
			(void)exchange_handle_msg(ex_node->ex, msg);
			nni_aio_finish(&ex_node->saio, 0, 0);
		} else {
			if (nni_lmq_full(&ex_node->send_messages)) {
				log_error("lmq is full, get oldest msg and free\n");
				if (nni_lmq_get(&ex_node->send_messages, &tmsg) == 0) {
					nni_msg_free(tmsg);
				}
			}
			if (nni_lmq_put(&ex_node->send_messages, msg) != 0) {
				log_error("Warning! exchange lost message!!\n");
			}
			nni_mtx_unlock(&ex_node->mtx);
		}
	}

	nni_aio_finish(aio, 0, 0);
	return;
}

static void
exchange_send_cb(void *arg)
{
	exchange_node_t *ex_node = arg;
	nni_msg *    msg = NULL;

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

	if (nni_lmq_get(&ex_node->send_messages, &msg) == 0) {
		nni_mtx_unlock(&ex_node->mtx);
		(void)exchange_handle_msg(ex_node->ex, msg);
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
exchange_sock_add_exchange(void *arg, const void *v, size_t sz, nni_opt_type t)
{
	exchange_sock_t *s = arg;
	int rv;

	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);

	exchange_t *ex = (exchange_t *)(*(void **)v);
	rv = exchange_add_ex(s, ex);

	return (rv);
}


static nni_option exchange_sock_options[] = {
	{
	    .o_name = NNG_OPT_EXCHANGE_ADD,
	    .o_set  = exchange_sock_add_exchange,
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
