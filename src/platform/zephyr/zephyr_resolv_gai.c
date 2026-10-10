//
// Copyright 2025 EMQX. All rights reserved.
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr DNS resolution via getaddrinfo.
// Zephyr supports getaddrinfo when CONFIG_DNS_RESOLVER is enabled.
// Falls back to numerical IP addresses if DNS is unavailable.

#include "core/nng_impl.h"

#ifdef NNG_USE_POSIX_RESOLV_GAI
#ifdef NNG_PLATFORM_ZEPHYR

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0
#endif

#ifndef NNG_HAVE_INET6
#undef NNG_ENABLE_IPV6
#endif

#include "platform/zephyr/zephyr_impl.h"

static nni_mtx  resolv_mtx  = NNI_MTX_INITIALIZER;
static nni_cv   resolv_cv   = NNI_CV_INITIALIZER(&resolv_mtx);
static bool     resolv_fini = false;
static nni_list resolv_aios;
static nni_thr *resolv_thrs;
static int      resolv_num_thr;

typedef struct resolv_item resolv_item;
struct resolv_item {
	int           family;
	bool          passive;
	char         *host;
	char         *serv;
	nni_aio      *aio;
	nng_sockaddr *sa;
};

static void
resolv_free_item(resolv_item *item)
{
	nni_strfree(item->serv);
	nni_strfree(item->host);
	NNI_FREE_STRUCT(item);
}

static int parse_ip(const char *, nng_sockaddr *, bool);

static void
resolv_cancel(nni_aio *aio, void *arg, int rv)
{
	resolv_item *item = arg;

	nni_mtx_lock(&resolv_mtx);
	if (item != nni_aio_get_prov_data(aio)) {
		// Already canceled — resolved by a different path.
		nni_mtx_unlock(&resolv_mtx);
		return;
	}
	nni_aio_set_prov_data(aio, NULL);
	if (nni_aio_list_active(aio)) {
		// Not yet picked up — safe to discard.
		nni_aio_list_remove(aio);
		nni_mtx_unlock(&resolv_mtx);
		resolv_free_item(item);
	} else {
		// resolv_task is running; let it finish but drop
		// our interest in the result.
		item->aio = NULL;
		item->sa  = NULL;
		nni_mtx_unlock(&resolv_mtx);
	}
	nni_aio_finish_error(aio, rv);
}

static int
posix_gai_errno(int rv)
{
	switch (rv) {
	case 0:
		return (0);
	case EAI_MEMORY:
		return (NNG_ENOMEM);
	case EAI_SYSTEM:
		return (nni_plat_errno(errno));
	case EAI_NONAME:
#ifdef EAI_NODATA
	case EAI_NODATA:
#endif
	case EAI_SERVICE:
		return (NNG_EADDRINVAL);
	case EAI_BADFLAGS:
		return (NNG_EINVAL);
	case EAI_SOCKTYPE:
		return (NNG_ENOTSUP);
#ifdef EAI_AGAIN
	case EAI_AGAIN:
		return (NNG_EAGAIN);
#endif
	default:
		return (NNG_ESYSERR + rv);
	}
}

static int
resolv_task(resolv_item *item)
{
	struct addrinfo  hints;
	struct addrinfo *results;
	struct addrinfo *probe;
	int              rv;

	memset(&hints, 0, sizeof(hints));
#ifdef AI_ADDRCONFIG
	hints.ai_flags = AI_ADDRCONFIG;
#endif
	if (item->passive) {
		hints.ai_flags |= AI_PASSIVE;
	}
	hints.ai_family   = item->family;
	hints.ai_socktype = SOCK_STREAM;

	if (item->serv != NULL) {
		long  port;
		char *end;
		port = strtol(item->serv, &end, 10);
		if (*end == '\0') {
			hints.ai_flags |= AI_NUMERICSERV;
			if ((port < 0) || (port > 0xffff)) {
				return (NNG_EADDRINVAL);
			}
		}
	}

	if ((rv = getaddrinfo(item->host, item->serv, &hints, &results)) != 0) {
		return (posix_gai_errno(rv));
	}

	rv = NNG_EADDRINVAL;
	for (probe = results; probe != NULL; probe = probe->ai_next) {
		if (probe->ai_addr->sa_family == AF_INET) {
			break;
		}
#ifdef NNG_ENABLE_IPV6
		if (probe->ai_addr->sa_family == AF_INET6) {
			break;
		}
#endif
	}

	nni_mtx_lock(&resolv_mtx);
	if ((probe != NULL) && (item->aio != NULL)) {
		struct sockaddr_in *sin;
#ifdef NNG_ENABLE_IPV6
		struct sockaddr_in6 *sin6;
#endif
		nng_sockaddr *sa = item->sa;

		switch (probe->ai_addr->sa_family) {
		case AF_INET:
			rv                 = 0;
			sin                = (void *) probe->ai_addr;
			sa->s_in.sa_family = NNG_AF_INET;
			sa->s_in.sa_port   = sin->sin_port;
			sa->s_in.sa_addr   = sin->sin_addr.s_addr;
			break;
#ifdef NNG_ENABLE_IPV6
		case AF_INET6:
			rv                  = 0;
			sin6                = (void *) probe->ai_addr;
			sa->s_in6.sa_family = NNG_AF_INET6;
			sa->s_in6.sa_port   = sin6->sin6_port;
			sa->s_in6.sa_scope  = sin6->sin6_scope_id;
			memcpy(sa->s_in6.sa_addr, sin6->sin6_addr.s6_addr, 16);
			break;
#endif
		}
	}
	nni_mtx_unlock(&resolv_mtx);

	if (results != NULL) {
		freeaddrinfo(results);
	}
	return (rv);
}

void
nni_resolv_ip(const char *host, const char *serv, int af, bool passive,
    nng_sockaddr *sa, nni_aio *aio)
{
	resolv_item *item;
	int          rv;

	if (nni_aio_begin(aio) != 0) {
		return;
	}

	// Step 1 — fast path: try numeric IP via parse_ip (uses
	// getaddrinfo with AI_NUMERICHOST).  Returns only for numeric
	// IPs like "127.0.0.1" — hostnames fall through to step 2.
	{
		size_t hl = strlen(host);
		size_t sl = serv ? strlen(serv) : 0;
		char  *buf;
		int    fast_ok = 0;

		buf = nni_alloc(hl + sl + 2);
		if (buf != NULL) {
			memcpy(buf, host, hl);
			if (sl > 0) {
				buf[hl] = ':';
				memcpy(buf + hl + 1, serv, sl + 1);
			} else {
				buf[hl] = '\0';
			}
			if (parse_ip(buf, sa, serv != NULL) == 0) {
				fast_ok = 1;
			}
			nni_free(buf, hl + sl + 2);
		}
		if (fast_ok) {
			nni_aio_finish(aio, 0, 0);
			return;
		}
	}

	// Step 2 — slow path: DNS resolution for hostnames.
	switch (af) {
	case NNG_AF_INET:
		af = AF_INET;
		break;
	case NNG_AF_INET6:
		af = AF_INET6;
		break;
	case NNG_AF_UNSPEC:
		af = AF_UNSPEC;
		break;
	default:
		nni_aio_finish_error(aio, NNG_EADDRINVAL);
		return;
	}

	if ((item = NNI_ALLOC_STRUCT(item)) == NULL) {
		nni_aio_finish_error(aio, NNG_ENOMEM);
		return;
	}
	item->family  = af;
	item->passive = passive;
	item->host    = nni_strdup(host);
	item->serv    = serv ? nni_strdup(serv) : NULL;
	item->aio     = aio;
	item->sa      = sa;

	// Guard against strdup failure.
	if ((item->host == NULL) || (serv != NULL && item->serv == NULL)) {
		resolv_free_item(item);
		nni_aio_finish_error(aio, NNG_ENOMEM);
		return;
	}

	nni_mtx_lock(&resolv_mtx);
	if (resolv_fini) {
		nni_mtx_unlock(&resolv_mtx);
		resolv_free_item(item);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}
	nni_aio_set_prov_data(aio, item);
	rv = nni_aio_schedule(aio, resolv_cancel, item);
	if (rv != 0) {
		nni_mtx_unlock(&resolv_mtx);
		resolv_free_item(item);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_mtx_unlock(&resolv_mtx);

	// Synchronous resolution directly on caller's thread.
	rv = resolv_task(item);

	// resolv_cancel may have fired via the expire thread; if so,
	// item->aio is NULL and the aio was already finished.
	if (item->aio != NULL) {
		if (rv == 0) {
			nni_aio_finish(aio, 0, 0);
		} else {
			nni_aio_finish_error(aio, rv);
		}
	}
	resolv_free_item(item);
}

static void
resolv_worker(void *arg)
{
	NNI_ARG_UNUSED(arg);

	for (;;) {
		resolv_item *item;
		nni_aio     *aio;
		nng_sockaddr *sa;
		int           rv;

		nni_mtx_lock(&resolv_mtx);
		while (!resolv_fini && nni_list_empty(&resolv_aios)) {
			nni_cv_wait(&resolv_cv);
		}
		if (resolv_fini) {
			nni_mtx_unlock(&resolv_mtx);
			break;
		}
		item = nni_list_first(&resolv_aios);
		nni_list_remove(&resolv_aios, item);
		nni_mtx_unlock(&resolv_mtx);

		aio = item->aio;
		sa  = item->sa;

		rv = resolv_task(item);
		resolv_free_item(item);

		if (aio != NULL) {
			if (rv == 0) {
				nni_aio_finish(aio, 0, 0);
			} else {
				nni_aio_finish_error(aio, rv);
			}
		}
	}
}

static int
parse_ip(const char *addr, nng_sockaddr *sa, bool want_port)
{
	struct addrinfo  hints;
	struct addrinfo *results;
	int              rv;
	char            *port;
	char            *host;
	char            *buf;
	size_t           buf_len;

#ifdef NNG_ENABLE_IPV6
	bool  v6      = false;
	bool  wrapped = false;
	char *s;
#endif

	if (addr == NULL) {
		addr = "";
	}

	buf_len = strlen(addr) + 1;
	if ((buf = nni_alloc(buf_len)) == NULL) {
		return (NNG_ENOMEM);
	}
	memcpy(buf, addr, buf_len);
	host = buf;
#ifdef NNG_ENABLE_IPV6
	if (*host == '[') {
		v6      = true;
		wrapped = true;
		host++;
	} else {
		for (s = host; *s != '\0'; s++) {
			if (*s == '.') {
				break;
			}
			if (*s == ':') {
				v6 = true;
				break;
			}
		}
	}
	for (port = host; *port != '\0'; port++) {
		if (wrapped) {
			if (*port == ']') {
				*port++ = '\0';
				wrapped = false;
				break;
			}
		} else if (!v6) {
			if (*port == ':') {
				break;
			}
		}
	}

	if (wrapped) {
		// Never got the closing bracket.
		rv = NNG_EADDRINVAL;
		goto done;
	}
#else  // NNG_ENABLE_IPV6
	for (port = host; *port != '\0'; port++) {
		if (*port == ':') {
			break;
		}
	}
#endif // NNG_ENABLE_IPV6

	if ((!want_port) && (*port != '\0')) {
		rv = NNG_EADDRINVAL;
		goto done;
	} else if (*port == ':') {
		*port++ = '\0';
	}

	if (*port == '\0') {
		port = "0";
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_NUMERICSERV | AI_NUMERICHOST | AI_PASSIVE;
#ifdef NNG_ENABLE_IPV6
	if (v6) {
		hints.ai_family = AF_INET6;
	}
#else
	hints.ai_family = AF_INET;
#endif
#ifdef AI_ADDRCONFIG
	hints.ai_flags |= AI_ADDRCONFIG;
#endif

	rv = getaddrinfo(host, port, &hints, &results);
	if ((rv != 0) || (results == NULL)) {
		rv = posix_gai_errno(rv != 0 ? rv : EAI_NONAME);
		goto done;
	}
	nni_posix_sockaddr2nn(
	    sa, (void *) results->ai_addr, results->ai_addrlen);
	freeaddrinfo(results);

done:
	nni_free(buf, buf_len);
	return (rv);
}

int
nni_parse_ip(const char *addr, nni_sockaddr *sa)
{
	return (parse_ip(addr, sa, false));
}

int
nni_parse_ip_port(const char *addr, nni_sockaddr *sa)
{
	return (parse_ip(addr, sa, true));
}

int
nni_posix_resolv_sysinit(void)
{
	// Zephyr: use synchronous resolution (no worker threads).
	// The thread-pool approach causes init-order races with
	// pthread mutex/condvar registration on Zephyr.
	nni_aio_list_init(&resolv_aios);
	nni_mtx_init(&resolv_mtx);
	nni_cv_init(&resolv_cv, &resolv_mtx);
	resolv_fini = false;
	resolv_num_thr = 0;
	return (0);
}

void
nni_posix_resolv_sysfini(void)
{
	nni_mtx_lock(&resolv_mtx);
	resolv_fini = true;
	nni_cv_wake(&resolv_cv);
	nni_mtx_unlock(&resolv_mtx);

	for (int i = 0; i < resolv_num_thr; i++) {
		nni_thr_fini(&resolv_thrs[i]);
	}
	NNI_FREE_STRUCTS(resolv_thrs, resolv_num_thr);
	resolv_thrs     = NULL;
	resolv_num_thr  = 0;
	resolv_fini     = false;
}

#endif // NNG_PLATFORM_ZEPHYR
#endif // NNG_USE_POSIX_RESOLV_GAI
