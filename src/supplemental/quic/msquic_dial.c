//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// #if defined(NNG_ENABLE_QUIC) // && defined(NNG_QUIC_MSQUIC)

#include "core/defs.h"
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

struct nni_quic_dialer {
	nni_aio                *qconaio; // for quic connection
	nni_aio                *qstrmaio; // for quic stream
	nni_list                connq;   // pending connections/quic streams
	bool                    closed;
	bool                    nodelay;
	bool                    keepalive;
	struct sockaddr_storage src;
	size_t                  srclen;
	nni_mtx                 mtx;
	nni_atomic_u64          ref;
	nni_atomic_bool         fini;

	// MsQuic
	HQUIC                   qconn; // quic connection
};

static void quic_dialer_cb();
static void quic_dialer_strm_cb();

int
nni_quic_dialer_init(void **argp)
{
	nni_quic_dialer *d;

	if ((d = NNI_ALLOC_STRUCT(d)) == NULL) {
		return (NNG_ENOMEM);
	}

	nni_mtx_init(&d->mtx);
	d->closed = false;
	nni_aio_alloc(&d->qconaio, quic_dialer_cb, (void *)d);
	nni_aio_alloc(&d->qconaio, quic_dialer_strm_cb, (void *)d);
	nni_aio_list_init(&d->connq);
	nni_atomic_init_bool(&d->fini);
	nni_atomic_init64(&d->ref);
	nni_atomic_inc64(&d->ref);

	*argp = d;
	return 0;
}

void
nni_quic_dialer_fini(void *arg)
{
	nni_quic_dialer *d = arg;
	NNI_ARG_UNUSED(d);

	return;
}

void
nni_quic_dial(void *arg, const char *url, nni_aio *aio)
{
	nni_quic_dialer *d = arg;
	NNI_ARG_UNUSED(d);
}

void
nni_quic_dialer_close(void *arg)
{
	nni_quic_dialer *d = arg;
	NNI_ARG_UNUSED(d);
}

// #endif
