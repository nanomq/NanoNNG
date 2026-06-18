//
// Copyright 2025 EMQX. All rights reserved.
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr threading -- uses pthread via Zephyr's POSIX API.
// Key differences from POSIX:
//   - No fork() → pthread_atfork removed
//   - No SIGPIPE → signal suppression removed or guarded
//   - getpid() may not be available
//   - sysconf(_SC_NPROCESSORS_ONLN) may not be available

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Forward declarations for Zephyr-specific init helpers
extern int  nni_posix_pollq_sysinit(void);
extern void nni_posix_pollq_sysfini(void);
extern int  nni_posix_resolv_sysinit(void);
extern void nni_posix_resolv_sysfini(void);

static pthread_mutex_t nni_plat_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int             nni_plat_inited    = 0;

// Zephyr has no fork, so this guard is not needed, but we keep
// the variable for API compatibility.
#ifndef NNG_PLATFORM_ZEPHYR
static int             nni_plat_forked    = 0;
#endif

pthread_condattr_t  nni_cvattr;
pthread_mutexattr_t nni_mxattr;
pthread_attr_t      nni_thrattr;

// ---- mutex ----

void
nni_plat_mtx_init(nni_plat_mtx *mtx)
{
	// Zephyr: try once with attr, then once without. No infinite retry.
	if (pthread_mutex_init(&mtx->mtx, &nni_mxattr) != 0) {
		(void) pthread_mutex_init(&mtx->mtx, NULL);
	}
}
void
nni_plat_mtx_fini(nni_plat_mtx *mtx)
{
	(void) pthread_mutex_destroy(&mtx->mtx);
}

void
nni_plat_mtx_lock(nni_plat_mtx *mtx)
{
	int rv;
	rv = pthread_mutex_lock(&mtx->mtx);
	if (rv == EINVAL) {
		// Zephyr: static init may not have registered the mutex.
		// Try to register now (best-effort; may fail if pool full).
		(void) pthread_mutex_init(&mtx->mtx, NULL);
		rv = pthread_mutex_lock(&mtx->mtx);
	}
	if (rv != 0) {
		nni_panic("pthread_mutex_lock: %s", strerror(rv));
	}
}

void
nni_plat_mtx_unlock(nni_plat_mtx *mtx)
{
	int rv;
	if ((rv = pthread_mutex_unlock(&mtx->mtx)) != 0) {
		nni_panic("pthread_mutex_unlock: %s", strerror(rv));
	}
}

// ---- rwlock ----

void
nni_rwlock_init(nni_rwlock *rwl)
{
	while (pthread_rwlock_init(&rwl->rwl, NULL) != 0) {
		nni_msleep(10);
	}
}

void
nni_rwlock_fini(nni_rwlock *rwl)
{
	int rv;
	if ((rv = pthread_rwlock_destroy(&rwl->rwl)) != 0) {
		nni_panic("pthread_rwlock_destroy: %s", strerror(rv));
	}
}

void
nni_rwlock_rdlock(nni_rwlock *rwl)
{
	int rv;
	rv = pthread_rwlock_rdlock(&rwl->rwl);
	if (rv == EINVAL) {
		(void) pthread_rwlock_init(&rwl->rwl, NULL);
		rv = pthread_rwlock_rdlock(&rwl->rwl);
	}
	if (rv != 0) {
		nni_panic("pthread_rwlock_rdlock: %s", strerror(rv));
	}
}

void
nni_rwlock_wrlock(nni_rwlock *rwl)
{
	int rv;
	rv = pthread_rwlock_wrlock(&rwl->rwl);
	if (rv == EINVAL) {
		(void) pthread_rwlock_init(&rwl->rwl, NULL);
		rv = pthread_rwlock_wrlock(&rwl->rwl);
	}
	if (rv != 0) {
		nni_panic("pthread_rwlock_wrlock: %s", strerror(rv));
	}
}

void
nni_rwlock_unlock(nni_rwlock *rwl)
{
	int rv;
	if ((rv = pthread_rwlock_unlock(&rwl->rwl)) != 0) {
		nni_panic("pthread_rwlock_unlock: %s", strerror(rv));
	}
}

// ---- condition variable ----

void
nni_plat_cv_init(nni_plat_cv *cv, nni_plat_mtx *mtx)
{
	// Zephyr: try once. No infinite retry.
	if (pthread_cond_init(&cv->cv, &nni_cvattr) != 0) {
		(void) pthread_cond_init(&cv->cv, NULL);
	}
	cv->mtx = mtx;
}

void
nni_plat_cv_fini(nni_plat_cv *cv)
{
	int rv;
	if ((rv = pthread_cond_destroy(&cv->cv)) != 0) {
		nni_panic("pthread_cond_destroy: %s", strerror(rv));
	}
	cv->mtx = NULL;
}

void
nni_plat_cv_wake(nni_plat_cv *cv)
{
	int rv = pthread_cond_broadcast(&cv->cv);
	if (rv == EINVAL) {
		(void) pthread_cond_init(&cv->cv, NULL);
		rv = pthread_cond_broadcast(&cv->cv);
	}
	if (rv != 0) {
		nni_panic("pthread_cond_broadcast: %s", strerror(rv));
	}
}

void
nni_plat_cv_wake1(nni_plat_cv *cv)
{
	int rv = pthread_cond_signal(&cv->cv);
	if (rv == EINVAL) {
		(void) pthread_cond_init(&cv->cv, NULL);
		rv = pthread_cond_signal(&cv->cv);
	}
	if (rv != 0) {
		nni_panic("pthread_cond_signal: %s", strerror(rv));
	}
}

void
nni_plat_cv_wait(nni_plat_cv *cv)
{
	int rv = pthread_cond_wait(&cv->cv, &cv->mtx->mtx);
	if (rv == EINVAL) {
		(void) pthread_cond_init(&cv->cv, NULL);
		rv = pthread_cond_wait(&cv->cv, &cv->mtx->mtx);
	}
	if (rv != 0) {
		nni_panic("pthread_cond_wait: %s", strerror(rv));
	}
}

int
nni_plat_cv_until(nni_plat_cv *cv, nni_time until)
{
	struct timespec ts;
	int             rv;

	ts.tv_sec  = until / 1000;
	ts.tv_nsec = (until % 1000) * 1000000;

	switch ((rv = pthread_cond_timedwait(&cv->cv, &cv->mtx->mtx, &ts))) {
	case 0:
		return (0);
	case ETIMEDOUT:
	case EAGAIN:
		return (NNG_ETIMEDOUT);
	}
	nni_panic("pthread_cond_timedwait: %s", strerror(rv));
	return (NNG_EINVAL);
}

// ---- thread ----

// Unlike POSIX, we do not suppress SIGPIPE on Zephyr.
// Zephyr's signal support is minimal and SIGPIPE may not exist.

static void *
nni_plat_thr_main(void *arg)
{
	nni_plat_thr *thr = arg;
	thr->func(thr->arg);
	return (NULL);
}

int
nni_plat_thr_init(nni_plat_thr *thr, void (*fn)(void *), void *arg)
{
	int rv;

	thr->func = fn;
	thr->arg  = arg;

	rv = pthread_create(&thr->tid, NULL, nni_plat_thr_main, thr);
	if (rv != 0) {
		return (NNG_ENOMEM);
	}
	return (0);
}

void
nni_plat_thr_fini(nni_plat_thr *thr)
{
	int rv;
	if ((rv = pthread_join(thr->tid, NULL))) {
		nni_panic("pthread_join: %s", strerror(rv));
	}
}

bool
nni_plat_thr_is_self(nni_plat_thr *thr)
{
	return (pthread_self() == thr->tid);
}

void
nni_plat_thr_set_name(nni_plat_thr *thr, const char *name)
{
	// Zephyr may support pthread_setname_np depending on toolchain.
	// If not, this is a no-op.
#if defined(NNG_HAVE_PTHREAD_SETNAME_NP) && !defined(NNG_PLATFORM_ZEPHYR)
	pthread_setname_np(thr ? thr->tid : pthread_self(), name);
#endif
	NNI_ARG_UNUSED(thr);
	NNI_ARG_UNUSED(name);
}

// ---- init / fini ----

// Zephyr has no fork -- remove the fork guard entirely.
#ifndef NNG_PLATFORM_ZEPHYR
void
nni_atfork_child(void)
{
	nni_plat_forked = 1;
}
#endif

int
nni_plat_init(int (*helper)(void))
{
	int rv;

#ifndef NNG_PLATFORM_ZEPHYR
	if (nni_plat_forked) {
		nni_panic("nng is not fork-reentrant safe");
	}
#endif

	if (nni_plat_inited) {
		return (0);
	}

	pthread_mutex_lock(&nni_plat_init_lock);
	if (nni_plat_inited) {
		pthread_mutex_unlock(&nni_plat_init_lock);
		return (0);
	}

	if ((pthread_mutexattr_init(&nni_mxattr) != 0) ||
	    (pthread_condattr_init(&nni_cvattr) != 0) ||
	    (pthread_attr_init(&nni_thrattr) != 0)) {
		pthread_mutex_unlock(&nni_plat_init_lock);
		return (NNG_ENOMEM);
	}

#if !defined(NNG_USE_GETTIMEOFDAY) && NNG_USE_CLOCKID != CLOCK_REALTIME
	// Zephyr may or may not support pthread_condattr_setclock.
	// If it fails, we fall back to CLOCK_REALTIME semantics.
	if (pthread_condattr_setclock(&nni_cvattr, NNG_USE_CLOCKID) != 0) {
		// Non-fatal: continue without monotonic clock support
	}
#endif

	// Error-check mutexes (best effort)
	(void) pthread_mutexattr_settype(&nni_mxattr, PTHREAD_MUTEX_ERRORCHECK);

	if ((rv = nni_posix_pollq_sysinit()) != 0) {
		pthread_mutex_unlock(&nni_plat_init_lock);
		pthread_mutexattr_destroy(&nni_mxattr);
		pthread_condattr_destroy(&nni_cvattr);
		pthread_attr_destroy(&nni_thrattr);
		return (rv);
	}

	if ((rv = nni_posix_resolv_sysinit()) != 0) {
		pthread_mutex_unlock(&nni_plat_init_lock);
		nni_posix_pollq_sysfini();
		pthread_mutexattr_destroy(&nni_mxattr);
		pthread_condattr_destroy(&nni_cvattr);
		pthread_attr_destroy(&nni_thrattr);
		return (rv);
	}

#ifndef NNG_PLATFORM_ZEPHYR
	// Zephyr has no fork, so pthread_atfork is not needed
	if (pthread_atfork(NULL, NULL, nni_atfork_child) != 0) {
		pthread_mutex_unlock(&nni_plat_init_lock);
		nni_posix_resolv_sysfini();
		nni_posix_pollq_sysfini();
		pthread_mutexattr_destroy(&nni_mxattr);
		pthread_condattr_destroy(&nni_cvattr);
		pthread_attr_destroy(&nni_thrattr);
		return (NNG_ENOMEM);
	}
#endif

	if ((rv = helper()) == 0) {
		nni_plat_inited = 1;
	}
	pthread_mutex_unlock(&nni_plat_init_lock);

	return (rv);
}

void
nni_plat_fini(void)
{
	pthread_mutex_lock(&nni_plat_init_lock);
	if (nni_plat_inited) {
		nni_posix_resolv_sysfini();
		nni_posix_pollq_sysfini();
		pthread_mutexattr_destroy(&nni_mxattr);
		pthread_condattr_destroy(&nni_cvattr);
		nni_plat_inited = 0;
	}
	pthread_mutex_unlock(&nni_plat_init_lock);
}

int
nni_plat_ncpu(void)
{
#ifdef _SC_NPROCESSORS_ONLN
	return (sysconf(_SC_NPROCESSORS_ONLN) > 0
	        ? sysconf(_SC_NPROCESSORS_ONLN) : 1);
#else
	return (1);
#endif
}

int
nni_plat_getpid(void)
{
	// Zephyr does not have traditional PIDs.
	// Return 0 as a placeholder.
	return (0);
}

#endif // NNG_PLATFORM_ZEPHYR
