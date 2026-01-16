#ifdef NANO_PLATFORM_WINDOWS
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

#include "nng/supplemental/nanolib/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void
fatal(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	fflush(stderr);
	exit(1);
}

void
nng_fatal(const char *msg, int rv)
{
	fatal("%s: %s", msg, nng_strerror(rv));
}

uint16_t
nano_pipe_get_local_port(nng_pipe p)
{
	int           rv;
	nng_sockaddr  addr;

	rv = nng_pipe_get_addr(p, NNG_OPT_LOCADDR, &addr);
	if (rv != 0)
		return 0;

	return htons(addr.s_in.sa_port);
}

uint16_t
nano_pipe_get_local_port6(nng_pipe p)
{
	int           rv;
	nng_sockaddr  addr;

	rv = nng_pipe_get_addr(p, NNG_OPT_LOCADDR, &addr);
	if (rv != 0)
		return 0;

	return htons(addr.s_in6.sa_port);
}
