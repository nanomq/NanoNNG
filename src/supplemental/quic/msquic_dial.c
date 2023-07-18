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

struct nni_quic_dialer {
};

int
nni_quic_dialer_init(void *arg)
{
	nni_quic_dialer *d = arg;
	NNI_ARG_UNUSED(d);

	return 0;
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
