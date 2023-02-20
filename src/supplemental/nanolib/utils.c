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