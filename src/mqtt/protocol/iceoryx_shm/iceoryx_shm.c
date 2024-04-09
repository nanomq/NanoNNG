//
// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "core/nng_impl.h"

#include "supplemental/iceoryx/iceoryx_api.h"

#define NNG_ICEORYX_SELF 0
#define NNG_ICEORYX_SELF_NAME "iceoryx-self"
#define NNG_ICEORYX_PEER 0
#define NNG_ICEORYX_PEER_NAME "iceoryx-peer"

typedef struct iceoryx_sock_s iceoryx_sock_t;
typedef struct iceoryx_pipe_s iceoryx_pipe_t;
typedef struct iceoryx_ctx_s  iceoryx_ctx_t;

static void iceoryx_sock_init(void *arg, nni_sock *sock);
static void iceoryx_sock_fini(void *arg);
static void iceoryx_sock_open(void *arg);
static void iceoryx_sock_close(void *arg);
static void iceoryx_sock_send(void *arg, nni_aio *aio);
static void iceoryx_sock_recv(void *arg, nni_aio *aio);
static void iceoryx_send_cb(void *arg);
static void iceoryx_recv_cb(void *arg);
//static void iceoryx_timer_cb(void *arg);

static int  iceoryx_pipe_init(void *arg, nni_pipe *pipe, void *s);
static void iceoryx_pipe_fini(void *arg);
static int  iceoryx_pipe_start(void *arg);
static void iceoryx_pipe_stop(void *arg);
static int  iceoryx_pipe_close(void *arg);

static void iceoryx_ctx_init(void *arg, void *sock);
static void iceoryx_ctx_fini(void *arg);
static void iceoryx_ctx_send(void *arg, nni_aio *aio);
static void iceoryx_ctx_recv(void *arg, nni_aio *aio);

struct iceoryx_ctx_s {
	iceoryx_sock_t *iceoryx_sock;
	nni_aio        *saio; // send aio
	nni_aio        *raio; // recv aio
	nni_list_node   sqnode;
	nni_list_node   rqnode;
};

struct iceoryx_sock_s {
	nni_atomic_bool    closed;
	iceoryx_ctx_t      master; // to which we delegate send/recv calls
	nni_mtx            mtx;    // more fine grained mutual exclusion
	iceoryx_pipe_t    *iceoryx_pipe;

	nni_list           recv_queue; // ctx pending to receive
	nni_list           send_queue; // ctx pending to send (only offline msg)
};

// A iceoryx_pipe_s is our per-pipe protocol private structure.
struct iceoryx_pipe_s {
	nni_atomic_bool   closed;
	nni_pipe         *pipe;
	iceoryx_sock_t   *iceoryx_sock;

	nni_aio           send_aio; // send aio to the underlying transport
	nni_aio           recv_aio; // recv aio to the underlying transport
	bool              busy;
};

static void
iceoryx_sock_init(void *arg, nni_sock *sock)
{
	NNI_ARG_UNUSED(sock);
	iceoryx_sock_t *s = arg;

	nni_atomic_init_bool(&s->closed);
	nni_atomic_set_bool(&s->closed, false);

	iceoryx_ctx_init(&s->master, s);
	nni_mtx_init(&s->mtx);
	s->iceoryx_pipe = NULL;

	NNI_LIST_INIT(&s->recv_queue, iceoryx_ctx_t, rqnode);
	NNI_LIST_INIT(&s->send_queue, iceoryx_ctx_t, sqnode);
}

static void
iceoryx_sock_fini(void *arg)
{
	iceoryx_sock_t *s = arg;
	iceoryx_ctx_fini(&s->master);
	nni_mtx_fini(&s->mtx);
}

static void
iceoryx_sock_open(void *arg)
{
	NNI_ARG_UNUSED(arg);
}

static void
iceoryx_sock_close(void *arg)
{
	iceoryx_sock_t *s = arg;

	nni_atomic_set_bool(&s->closed, true);
}

static void
iceoryx_sock_send(void *arg, nni_aio *aio)
{
	iceoryx_sock_t *s = arg;
	iceoryx_ctx_send(&s->master, aio);
}

static void
iceoryx_sock_recv(void *arg, nni_aio *aio)
{
	iceoryx_sock_t *s = arg;
	iceoryx_ctx_recv(&s->master, aio);
}

static void
iceoryx_send_cb(void *arg)
{
	iceoryx_pipe_t *p = arg;
	iceoryx_sock_t *s = p->iceoryx_sock;
	int             rv;

	if ((rv = nni_aio_result(&p->send_aio)) != 0) {
		// We failed to send... clean up and deal with it.
		nni_msg_free(nni_aio_get_msg(&p->send_aio));
		nni_aio_set_msg(&p->send_aio, NULL);
		// nni_pipe_close(p->pipe);
		return;
	}
	nni_mtx_lock(&s->mtx);

	p->busy     = false;
	if (nni_atomic_get_bool(&s->closed) ||
	    nni_atomic_get_bool(&p->closed)) {
		// This occurs if the iceoryx_pipe_close has been called.
		// In that case we don't want any more processing.
		nni_mtx_unlock(&s->mtx);
		return;
	}

	p->busy = false;
	nni_mtx_unlock(&s->mtx);
	return;
}

static void
iceoryx_recv_cb(void *arg)
{
	int             rv;
	iceoryx_pipe_t *p = arg;
	iceoryx_sock_t *s = p->iceoryx_sock;

	if ((rv = nni_aio_result(&p->recv_aio)) != 0) {
		log_warn("iceoryx client recv error %d!", rv);
		// nni_pipe_close(p->pipe);
		return;
	}

	nni_mtx_lock(&s->mtx);
	nni_msg *msg = nni_aio_get_msg(&p->recv_aio);
	nni_aio_set_msg(&p->recv_aio, NULL);
	if (nni_atomic_get_bool(&s->closed) ||
	    nni_atomic_get_bool(&p->closed)) {
		// free msg and dont return data when pipe is closed.
		if (msg) {
			nni_msg_free(msg);
		}
		nni_mtx_unlock(&s->mtx);
		return;
	}

	// schedule another receive
	nni_pipe_recv(p->pipe, &p->recv_aio);

	nni_mtx_unlock(&s->mtx);
	return;
}

static inline void
iceoryx_recv(nni_aio *aio, iceoryx_ctx_t *ctx)
{
	NNI_ARG_UNUSED(aio);
	NNI_ARG_UNUSED(ctx);
}

static inline void
iceoryx_send(nni_aio *aio, iceoryx_ctx_t *ctx)
{
	NNI_ARG_UNUSED(aio);
	NNI_ARG_UNUSED(ctx);
}

static void
iceoryx_ctx_init(void *arg, void *sock)
{
	iceoryx_ctx_t  *ctx = arg;
	iceoryx_sock_t *s   = sock;

	ctx->iceoryx_sock = s;
	NNI_LIST_NODE_INIT(&ctx->sqnode);
	NNI_LIST_NODE_INIT(&ctx->rqnode);
}

static void
iceoryx_ctx_fini(void *arg)
{
	iceoryx_ctx_t  *ctx = arg;
	iceoryx_sock_t *s   = ctx->iceoryx_sock;
	nni_aio        *aio;

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
iceoryx_ctx_send(void *arg, nni_aio *aio)
{
	iceoryx_ctx_t  *ctx = arg;
	iceoryx_sock_t *s   = ctx->iceoryx_sock;
	iceoryx_pipe_t *p;
	nni_msg        *msg;

	if (nni_aio_begin(aio) != 0) {
		return;
	}

	nni_mtx_lock(&s->mtx);
	if (nni_atomic_get_bool(&s->closed)) {
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}

	msg = nni_aio_get_msg(aio);
	if (msg == NULL) {
		nni_mtx_unlock(&s->mtx);
		nni_aio_set_msg(aio, NULL);
		nni_aio_finish_error(aio, NNG_EPROTO);
		return;
	}

	p = s->iceoryx_pipe; // TODO At this stage. iceoryx_pipe will not be null until sock init finished
	if (p == NULL) {
		if (!nni_list_active(&s->send_queue, ctx)) {
			// cache ctx
			ctx->saio = aio;
			nni_list_append(&s->send_queue, ctx);
			nni_mtx_unlock(&s->mtx);
			log_warn("client sending msg while disconnected! cached");
		} else {
			nni_msg_free(msg);
			nni_mtx_unlock(&s->mtx);
			nni_aio_set_msg(aio, NULL);
			nni_aio_finish_error(aio, NNG_ECLOSED);
			log_warn("ctx is already cached! drop msg");
		}
		return;
	}
	iceoryx_send(aio, ctx);
	nni_mtx_unlock(&s->mtx);
	log_trace("client sending msg now");
	return;
}

static void
iceoryx_ctx_recv(void *arg, nni_aio *aio)
{
	iceoryx_ctx_t  *ctx = arg;
	iceoryx_sock_t *s   = ctx->iceoryx_sock;
	iceoryx_pipe_t *p;

	if (nni_aio_begin(aio) != 0) {
		return;
	}

	nni_mtx_lock(&s->mtx);
	p = s->iceoryx_pipe;
	if (p == NULL) {
		goto wait;
	}
	if (nni_atomic_get_bool(&s->closed) ||
	    nni_atomic_get_bool(&p->closed)) {
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}
	// We don't need buffer. At least now. All msg are cached in iceoryx.
	// nni_lmq_get(&p->recv_messages, &msg)

	iceoryx_recv(aio, ctx);
	nni_mtx_unlock(&s->mtx);
	return;

	// no open pipe or msg waiting
wait:
	if (ctx->raio != NULL) {
		nni_mtx_unlock(&s->mtx);
		log_error("ERROR! former aio not finished!");
		nni_aio_finish_error(aio, NNG_ESTATE);
		return;
	}
	ctx->raio = aio;
	nni_list_append(&s->recv_queue, ctx);
	nni_mtx_unlock(&s->mtx);
	return;
}

// Pipe implementation
// But seems pipe is unnecessary for iceoryx shm communication.
// Here we just put code here for future.
static int
iceoryx_pipe_init(void *arg, nni_pipe *pipe, void *s)
{
	iceoryx_pipe_t *p = arg;

	nni_atomic_init_bool(&p->closed);
	nni_atomic_set_bool(&p->closed, true);

	p->pipe         = pipe;
	p->iceoryx_sock = s;

	nni_aio_init(&p->send_aio, iceoryx_send_cb, p);
	nni_aio_init(&p->recv_aio, iceoryx_recv_cb, p);

	return (0);
}

static void
iceoryx_pipe_fini(void *arg)
{
	iceoryx_pipe_t *p = arg;
	nni_msg        *msg;

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
}

static int
iceoryx_pipe_start(void *arg)
{
	iceoryx_pipe_t *p = arg;
	iceoryx_sock_t *s = p->iceoryx_sock;

	nni_mtx_lock(&s->mtx);
	nni_atomic_set_bool(&p->closed, false);
	s->iceoryx_pipe       = p;

	// TODO nni_pipe_recv(p->pipe, &p->recv_aio);
	nni_mtx_unlock(&s->mtx);

	return (0);
}

static void
iceoryx_pipe_stop(void *arg)
{
	iceoryx_pipe_t *p = arg;
	nni_aio_stop(&p->send_aio);
	nni_aio_stop(&p->recv_aio);
}

static int
iceoryx_pipe_close(void *arg)
{
	iceoryx_pipe_t *p = arg;
	iceoryx_sock_t *s = p->iceoryx_sock;

	nni_mtx_lock(&s->mtx);
	nni_atomic_set_bool(&p->closed, true);
	s->iceoryx_pipe = NULL;
	nni_aio_close(&p->send_aio);
	nni_aio_close(&p->recv_aio);

	nni_mtx_unlock(&s->mtx);
	return 0;
}

static nni_option iceoryx_ctx_options[] = {
	{
	    .o_name = NULL,
	},
};

static nni_proto_ctx_ops iceoryx_ctx_ops = {
	.ctx_size    = sizeof(iceoryx_ctx_t),
	.ctx_init    = iceoryx_ctx_init,
	.ctx_fini    = iceoryx_ctx_fini,
	.ctx_recv    = iceoryx_ctx_recv,
	.ctx_send    = iceoryx_ctx_send,
	.ctx_options = iceoryx_ctx_options,
};

static nni_option iceoryx_sock_options[] = {
	{
	    .o_name = NULL,
	},
};

static nni_proto_sock_ops iceoryx_sock_ops = {
	.sock_size    = sizeof(iceoryx_sock_t),
	.sock_init    = iceoryx_sock_init,
	.sock_fini    = iceoryx_sock_fini,
	.sock_open    = iceoryx_sock_open,
	.sock_close   = iceoryx_sock_close,
	.sock_options = iceoryx_sock_options,
	.sock_send    = iceoryx_sock_send,
	.sock_recv    = iceoryx_sock_recv,
};

static nni_proto_pipe_ops iceoryx_pipe_ops = {
	.pipe_size  = sizeof(iceoryx_pipe_t),
	.pipe_init  = iceoryx_pipe_init,
	.pipe_fini  = iceoryx_pipe_fini,
	.pipe_start = iceoryx_pipe_start,
	.pipe_close = iceoryx_pipe_close,
	.pipe_stop  = iceoryx_pipe_stop,
};

static nni_proto iceoryx_proto = {
	.proto_version  = NNI_PROTOCOL_VERSION,
	.proto_self     = { NNG_ICEORYX_SELF, NNG_ICEORYX_SELF_NAME },
	.proto_peer     = { NNG_ICEORYX_PEER, NNG_ICEORYX_PEER_NAME },
	.proto_flags    = NNI_PROTO_FLAG_SNDRCV,
	.proto_sock_ops = &iceoryx_sock_ops,
	.proto_pipe_ops = &iceoryx_pipe_ops,
	.proto_ctx_ops  = &iceoryx_ctx_ops,
};

int
nng_iceoryx_open(nng_socket *sock)
{
	return (nni_proto_open(sock, &iceoryx_proto));
}
