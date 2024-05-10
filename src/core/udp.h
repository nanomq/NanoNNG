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

extern int nni_udp_dialer_alloc(nng_stream_dialer **, const nng_url *);
// TODO extern int nni_udp_listener_alloc(nng_stream_listener **, const nng_url *);

#endif // CORE_UDP_H

