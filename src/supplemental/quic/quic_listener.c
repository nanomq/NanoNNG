//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
#include "quic_api.h"
#include "core/nng_impl.h"
#include "msquic.h"

#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
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

struct quic_listener {
	nng_stream_listener ops;
	const char *        host;
	const char *        port;
	void *              l; // platform listener
};

int
nni_quic_listener_alloc(nng_stream_listener **lp, const nni_url *url)
{
	int          rv;
	const char * h, *p;

	if ((rv = nni_init()) != 0) {
		return (rv);
	}

	h = url->u_hostname;
	// Wildcard special case, which means bind to INADDR_ANY.
	if ((h != NULL) && ((strcmp(h, "*") == 0) || (strcmp(h, "") == 0))) {
		h = NULL;
	}

	p = url->u_port;
	if (p == NULL) {
		return NNG_EADDRINVAL;
	}

	return (quic_listener_alloc_addr(lp, h, p));
}

