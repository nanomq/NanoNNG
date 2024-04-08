//
// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "supplemental/iceoryx/iceoryx_api.h"

#define NNG_ICEORYX_SELF 0
#define NNG_ICEORYX_SELF_NAME "iceoryx-self"
#define NNG_ICEORYX_PEER 0
#define NNG_ICEORYX_PEER_NAME "iceoryx-peer"

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
