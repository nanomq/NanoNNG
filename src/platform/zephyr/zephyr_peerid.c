//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr peer credential -- not supported.
// Peer credentials are only used for AF_UNIX (IPC) transport,
// which is not available on Zephyr.  Return ENOTSUP.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

int
nni_posix_peerid(int fd, uint64_t *euid, uint64_t *egid, uint64_t *prid,
    uint64_t *znid)
{
	NNI_ARG_UNUSED(fd);
	NNI_ARG_UNUSED(euid);
	NNI_ARG_UNUSED(egid);
	NNI_ARG_UNUSED(prid);
	NNI_ARG_UNUSED(znid);
	return (NNG_ENOTSUP);
}

#endif // NNG_PLATFORM_ZEPHYR
