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
static void iceoryx_timer_cb(void *arg);

static int  iceoryx_pipe_init(void *arg, nni_pipe *pipe, void *s);
static void iceoryx_pipe_fini(void *arg);
static int  iceoryx_pipe_start(void *arg);
static void iceoryx_pipe_stop(void *arg);
static int  iceoryx_pipe_close(void *arg);

static void iceoryx_ctx_init(void *arg, void *sock);
static void iceoryx_ctx_fini(void *arg);
static void iceoryx_ctx_send(void *arg, nni_aio *aio);
static void iceoryx_ctx_recv(void *arg, nni_aio *aio);

struct iceoryx_sock_s {
};

struct iceoryx_ctx_s {
};

// A iceoryx_pipe_s is our per-pipe protocol private structure.
struct iceoryx_pipe_s {
};

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
