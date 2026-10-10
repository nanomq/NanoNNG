//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr clock / sleep support.
// Zephyr POSIX layer provides clock_gettime and nanosleep when
// CONFIG_POSIX_API=y. If unavailable, fall back to Zephyr kernel API.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <errno.h>
#include <string.h>
#include <time.h>

#if defined(NNG_HAVE_CLOCK_GETTIME)

int
nni_time_get(uint64_t *sec, uint32_t *nsec)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
		*sec  = ts.tv_sec;
		*nsec = ts.tv_nsec;
		return (0);
	}
	return (nni_plat_errno(errno));
}

nni_time
nni_clock(void)
{
	struct timespec ts;
	nni_time        msec;

	if (clock_gettime(NNG_USE_CLOCKID, &ts) != 0) {
		nni_panic("clock_gettime failed: %s", strerror(errno));
	}

	msec = ts.tv_sec;
	msec *= 1000;
	msec += (ts.tv_nsec / 1000000);
	return (msec);
}

nni_time
nni_timestamp(void)
{
	struct timespec ts;
	nni_time        msec;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		nni_panic("clock_gettime failed: %s", strerror(errno));
	}
	msec = ts.tv_sec;
	msec *= 1000;
	msec += (ts.tv_nsec / 1000000);
	return (msec);
}

void
nni_msleep(nni_duration ms)
{
	struct timespec ts;

	ts.tv_sec  = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;

	while (ts.tv_sec || ts.tv_nsec) {
		if (nanosleep(&ts, &ts) == 0) {
			break;
		}
	}
}

#else
// Fallback: use Zephyr kernel API directly (no clock_gettime)

#include <zephyr/kernel.h>
#include <sys/time.h>

int
nni_time_get(uint64_t *sec, uint32_t *nsec)
{
	struct timeval tv;
	if (gettimeofday(&tv, NULL) == 0) {
		*sec  = tv.tv_sec;
		*nsec = tv.tv_usec * 1000;
		return (0);
	}
	return (nni_plat_errno(errno));
}

nni_time
nni_clock(void)
{
	return (nni_time) k_uptime_get();
}

nni_time
nni_timestamp(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((nni_time) tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void
nni_msleep(nni_duration ms)
{
	k_sleep(K_MSEC(ms));
}

#endif // NNG_HAVE_CLOCK_GETTIME

#endif // NNG_PLATFORM_ZEPHYR
