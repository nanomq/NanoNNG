//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr debug / abort / printf.
// Zephyr uses printk for kernel output; there is no stderr.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
nni_plat_abort(void)
{
	// On Zephyr, k_panic is preferred over abort()
	abort();
}

void
nni_plat_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void
nni_plat_println(const char *message)
{
	nni_plat_printf("%s\n", message);
}

const char *
nni_plat_strerror(int errnum)
{
	if (errnum > NNG_ESYSERR) {
		errnum -= NNG_ESYSERR;
	}
	return (strerror(errnum));
}

// Error number translation table (POSIX errno → NNG error codes)
static struct {
	int posix_err;
	int nng_err;
} nni_plat_errnos[] = {
	{ EINTR,	   NNG_EINTR	    },
	{ EINVAL,	   NNG_EINVAL	    },
	{ ENOMEM,	   NNG_ENOMEM	    },
	{ EACCES,	   NNG_EPERM	    },
	{ EADDRINUSE,	   NNG_EADDRINUSE   },
	{ EADDRNOTAVAIL,   NNG_EADDRINVAL   },
	{ EAFNOSUPPORT,	   NNG_ENOTSUP	    },
	{ EAGAIN,	   NNG_EAGAIN	    },
	{ EBADF,	   NNG_ECLOSED	    },
	{ EBUSY,	   NNG_EBUSY	    },
	{ ECONNABORTED,	   NNG_ECONNABORTED },
	{ ECONNREFUSED,	   NNG_ECONNREFUSED },
	{ ECONNRESET,	   NNG_ECONNRESET   },
	{ EHOSTUNREACH,	   NNG_EUNREACHABLE },
	{ ENETUNREACH,	   NNG_EUNREACHABLE },
	{ ENAMETOOLONG,	   NNG_EINVAL	    },
	{ ENOENT,	   NNG_ENOENT	    },
	{ ENOBUFS,	   NNG_ENOMEM	    },
	{ ENOPROTOOPT,	   NNG_ENOTSUP	    },
	{ ENOSYS,	   NNG_ENOTSUP	    },
	{ ENOTSUP,	   NNG_ENOTSUP	    },
	{ EPERM,	   NNG_EPERM	    },
	{ EPIPE,	   NNG_ECLOSED	    },
	{ EPROTO,	   NNG_EPROTO	    },
	{ EPROTONOSUPPORT, NNG_ENOTSUP	    },
	{ ETIMEDOUT,	   NNG_ETIMEDOUT    },
	{ EWOULDBLOCK,	   NNG_EAGAIN	    },
	{ ENOSPC,	   NNG_ENOSPC	    },
	{ EFBIG,	   NNG_ENOSPC	    },
	{ EDQUOT,	   NNG_ENOSPC	    },
	{ ENFILE,	   NNG_ENOFILES	    },
	{ EMFILE,	   NNG_ENOFILES	    },
	{ EEXIST,	   NNG_EEXIST	    },
	{ 0,		   0		    },
};

int
nni_plat_errno(int errnum)
{
	for (int i = 0; nni_plat_errnos[i].posix_err != 0; i++) {
		if (nni_plat_errnos[i].posix_err == errnum) {
			return (nni_plat_errnos[i].nng_err);
		}
	}
	return (NNG_ESYSERR + errnum);
}

#endif // NNG_PLATFORM_ZEPHYR
