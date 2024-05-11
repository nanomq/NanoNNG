//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef CORE_UDP_H
#define CORE_UDP_H

#include "core/nng_impl.h"

// These are interfaces we use for UDP internally.  These are not exposed
// to the public API.

typedef struct nni_udp_conn nni_udp_conn;
struct nni_udp_conn {
	nng_stream      stream;
	nni_list        readq;
	nni_list        writeq;
	bool            closed;
	nni_mtx         mtx;
	nni_plat_udp   *u;
	nni_reap_node   reap;
};

extern int nni_udp_dialer_alloc(nng_stream_dialer **, const nng_url *);
// TODO extern int nni_udp_listener_alloc(nng_stream_listener **, const nng_url *);

#endif // CORE_UDP_H

