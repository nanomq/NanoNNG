//
// Copyright 2025 EMQX. All rights reserved.
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr socket address conversion.
// Zephyr BSD socket API supports standard sockaddr structures.
// AF_UNIX is NOT supported and is removed from this implementation.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#ifndef NNG_HAVE_INET6
#undef NNG_ENABLE_IPV6
#endif

size_t
nni_posix_nn2sockaddr(void *sa, const nni_sockaddr *na)
{
	struct sockaddr_in    *sin;
	const nng_sockaddr_in *nsin;
	size_t                 sz = 0;
#ifdef NNG_ENABLE_IPV6
	struct sockaddr_in6    *sin6;
	const nng_sockaddr_in6 *nsin6;
#endif

	if ((sa == NULL) || (na == NULL)) {
		return (0);
	}
	switch (na->s_family) {
	case NNG_AF_INET:
		sin  = (void *) sa;
		nsin = &na->s_in;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family      = AF_INET;
		sin->sin_port        = nsin->sa_port;
		sin->sin_addr.s_addr = nsin->sa_addr;
		sz = sizeof(*sin);
		break;
#ifdef NNG_ENABLE_IPV6
	case NNG_AF_INET6:
		sin6  = (void *) sa;
		nsin6 = &na->s_in6;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family   = AF_INET6;
		sin6->sin6_port     = nsin6->sa_port;
		sin6->sin6_scope_id = nsin6->sa_scope;
		memcpy(sin6->sin6_addr.s6_addr, nsin6->sa_addr, 16);
		sz = sizeof(*sin6);
		break;
#endif
	}
	return (sz);
}

int
nni_posix_sockaddr2nn(nni_sockaddr *na, const void *sa_in, size_t sz)
{
	const struct sockaddr_in *sin;
#ifdef NNG_ENABLE_IPV6
	const struct sockaddr_in6 *sin6;
#endif

	if ((na == NULL) || (sa_in == NULL)) {
		return (NNG_EINVAL);
	}

	const struct sockaddr *sa = sa_in;
	switch (sa->sa_family) {
	case AF_INET:
		if (sz < sizeof(*sin)) {
			return (NNG_EINVAL);
		}
		sin                = sa_in;
		na->s_in.sa_family = NNG_AF_INET;
		na->s_in.sa_port   = sin->sin_port;
		na->s_in.sa_addr   = sin->sin_addr.s_addr;
		break;
#ifdef NNG_ENABLE_IPV6
	case AF_INET6:
		if (sz < sizeof(*sin6)) {
			return (NNG_EINVAL);
		}
		sin6                = sa_in;
		na->s_in6.sa_family = NNG_AF_INET6;
		na->s_in6.sa_port   = sin6->sin6_port;
		na->s_in6.sa_scope  = sin6->sin6_scope_id;
		memcpy(na->s_in6.sa_addr, sin6->sin6_addr.s6_addr, 16);
		break;
#endif
	default:
		return (NNG_ENOTSUP);
	}
	return (0);
}

#endif // NNG_PLATFORM_ZEPHYR
