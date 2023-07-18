//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#if defined(NNG_ENABLE_QUIC) // && defined(NNG_QUIC_MSQUIC)

void
nni_quic_dial(void *arg, const char *url, nni_aio *aio)
{
	nni_quic_dialer *d = arg;
}

void
nni_quic_dialer_close(void *arg)
{
	nni_quic_dialer *d = arg;
}

#endif
