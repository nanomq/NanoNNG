//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// #if defined(NNG_ENABLE_QUIC) // && defined(NNG_QUIC_MSQUIC)

#include "quic_api.h"
#include "core/nng_impl.h"
#include "msquic.h"

#include "nng/mqtt/mqtt_client.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "supplemental/mqtt/mqtt_msg.h"

#include "openssl/pem.h"
#include "openssl/x509.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "nng/mqtt/mqtt_client.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "supplemental/mqtt/mqtt_msg.h"

#include "openssl/pem.h"
#include "openssl/x509.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct nni_quic_conn {
	nng_stream      stream;
	nni_posix_pfd * pfd;
	nni_list        readq;
	nni_list        writeq;
	bool            closed;
	nni_mtx         mtx;
	nni_aio *       dial_aio;
	nni_quic_dialer *dialer;
	nni_reap_node   reap;
};

static void
quic_cb(nni_posix_pfd *pfd, unsigned events, void *arg)
{
}

static void
quic_free(void *arg)
{
	nni_quic_conn *c = arg;
	// nni_reap(&tcp_reap_list, c);
}

static void
quic_close(void *arg)
{
	nni_quic_conn *c = arg;
}

static void
quic_recv(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
}

static void
quic_send(void *arg, nni_aio *aio)
{
	nni_quic_conn *c = arg;
	int            rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}
}

static int
quic_get(void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	nni_quic_conn *c = arg;
	// return (nni_getopt(tcp_options, name, c, buf, szp, t));
}

static int
quic_set(void *arg, const char *name, const void *buf, size_t sz, nni_type t)
{
	nni_quic_conn *c = arg;
	// return (nni_setopt(tcp_options, name, c, buf, sz, t));
}

int
nni_msquic_quic_alloc(nni_quic_conn **cp, nni_quic_dialer *d)
{
	nni_quic_conn *c;
	if ((c = NNI_ALLOC_STRUCT(c)) == NULL) {
		return (NNG_ENOMEM);
	}

	c->closed = false;
	c->dialer = d;

	nni_mtx_init(&c->mtx);
	nni_aio_list_init(&c->readq);
	nni_aio_list_init(&c->writeq);

	c->stream.s_free  = quic_free;
	c->stream.s_close = quic_close;
	c->stream.s_recv  = quic_recv;
	c->stream.s_send  = quic_send;
	c->stream.s_get   = quic_get;
	c->stream.s_set   = quic_set;

	*cp = c;
	return (0);
}

void
nni_msquic_quic_init(nni_quic_conn *c, nni_posix_pfd *pfd)
{
}

void
nni_msquic_quic_start(nni_quic_conn *c, int nodelay, int keepalive)
{
	// Configure the initial quic connection.
	// ...TODO

	// nni_posix_pfd_set_cb(c->pfd, quic_cb, c);
}

