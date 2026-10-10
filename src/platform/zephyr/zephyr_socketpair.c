//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr socketpair -- not supported (no AF_UNIX).
// Used only for IPC transport testing; return ENOTSUP.
// Also provides IPC dialer/listener stubs (no AF_UNIX on Zephyr).

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

int
nni_socket_pair(int fds[2])
{
	NNI_ARG_UNUSED(fds);
	return (NNG_ENOTSUP);
}

// IPC transport stubs -- not supported on Zephyr (no AF_UNIX)
int
nni_ipc_dialer_alloc(nng_stream_dialer **dp, const nng_url *url)
{
	NNI_ARG_UNUSED(dp);
	NNI_ARG_UNUSED(url);
	return (NNG_ENOTSUP);
}

int
nni_ipc_listener_alloc(nng_stream_listener **lp, const nng_url *url)
{
	NNI_ARG_UNUSED(lp);
	NNI_ARG_UNUSED(url);
	return (NNG_ENOTSUP);
}

#endif // NNG_PLATFORM_ZEPHYR
