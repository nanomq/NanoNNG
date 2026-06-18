//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr atomics.
// Uses C11 stdatomic for 32-bit types; pthread-mutex for 64-bit to
// avoid __atomic_store_8 / libatomic dependency on 32-bit targets.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#ifdef NNG_HAVE_STDATOMIC

#include <stdatomic.h>
#include <pthread.h>

bool
nni_atomic_flag_test_and_set(nni_atomic_flag *f)
{
	return (atomic_flag_test_and_set(&f->f));
}

void
nni_atomic_flag_reset(nni_atomic_flag *f)
{
	atomic_flag_clear(&f->f);
}

void
nni_atomic_set_bool(nni_atomic_bool *v, bool b)
{
	atomic_store(&v->v, b);
}

bool
nni_atomic_get_bool(nni_atomic_bool *v)
{
	return (atomic_load(&v->v));
}

bool
nni_atomic_swap_bool(nni_atomic_bool *v, bool b)
{
	return (atomic_exchange(&v->v, b));
}

void
nni_atomic_init_bool(nni_atomic_bool *v)
{
	atomic_init(&v->v, false);
}

void
nni_atomic_init(nni_atomic_int *v)
{
	atomic_init(&v->v, 0);
}

void
nni_atomic_add(nni_atomic_int *v, int bump)
{
	(void) atomic_fetch_add_explicit(&v->v, bump, memory_order_relaxed);
}

void
nni_atomic_sub(nni_atomic_int *v, int bump)
{
	(void) atomic_fetch_sub_explicit(&v->v, bump, memory_order_relaxed);
}

int
nni_atomic_get(nni_atomic_int *v)
{
	return (atomic_load(&v->v));
}

void
nni_atomic_set(nni_atomic_int *v, int val)
{
	atomic_store(&v->v, val);
}

int
nni_atomic_swap(nni_atomic_int *v, int val)
{
	return (atomic_exchange(&v->v, val));
}

void
nni_atomic_inc(nni_atomic_int *v)
{
	(void) atomic_fetch_add_explicit(&v->v, 1, memory_order_relaxed);
}

void
nni_atomic_dec(nni_atomic_int *v)
{
	(void) atomic_fetch_sub_explicit(&v->v, 1, memory_order_relaxed);
}

int
nni_atomic_dec_nv(nni_atomic_int *v)
{
	return (atomic_fetch_sub(&v->v, 1) - 1);
}

bool
nni_atomic_cas(nni_atomic_int *v, int old, int new)
{
	return (atomic_compare_exchange_strong(&v->v, &old, new));
}

// ---- 64-bit atomics (pthread fallback) ----

void
nni_atomic_init64(nni_atomic_u64 *v)
{
	pthread_mutex_init(&v->m, NULL);
	v->v = 0;
}

void
nni_atomic_add64(nni_atomic_u64 *v, uint64_t bump)
{
	pthread_mutex_lock(&v->m);
	v->v += bump;
	pthread_mutex_unlock(&v->m);
}

void
nni_atomic_sub64(nni_atomic_u64 *v, uint64_t bump)
{
	pthread_mutex_lock(&v->m);
	v->v -= bump;
	pthread_mutex_unlock(&v->m);
}

uint64_t
nni_atomic_get64(nni_atomic_u64 *v)
{
	uint64_t rv;
	pthread_mutex_lock(&v->m);
	rv = v->v;
	pthread_mutex_unlock(&v->m);
	return (rv);
}

void
nni_atomic_set64(nni_atomic_u64 *v, uint64_t val)
{
	pthread_mutex_lock(&v->m);
	v->v = val;
	pthread_mutex_unlock(&v->m);
}

uint64_t
nni_atomic_swap64(nni_atomic_u64 *v, uint64_t val)
{
	uint64_t rv;
	pthread_mutex_lock(&v->m);
	rv   = v->v;
	v->v = val;
	pthread_mutex_unlock(&v->m);
	return (rv);
}

uint64_t
nni_atomic_dec64_nv(nni_atomic_u64 *v)
{
	uint64_t rv;
	pthread_mutex_lock(&v->m);
	v->v -= 1;
	rv = v->v;
	pthread_mutex_unlock(&v->m);
	return (rv);
}

void
nni_atomic_inc64(nni_atomic_u64 *v)
{
	pthread_mutex_lock(&v->m);
	v->v += 1;
	pthread_mutex_unlock(&v->m);
}

bool
nni_atomic_cas64(nni_atomic_u64 *v, uint64_t old, uint64_t new)
{
	bool rv = false;
	pthread_mutex_lock(&v->m);
	if (v->v == old) {
		v->v = new;
		rv   = true;
	}
	pthread_mutex_unlock(&v->m);
	return (rv);
}

// ---- Pointer atomics (C11) ----

void
nni_atomic_set_ptr(nni_atomic_ptr *v, void *p)
{
	atomic_store(&v->v, (atomic_uintptr_t)(uintptr_t)p);
}

void *
nni_atomic_get_ptr(nni_atomic_ptr *v)
{
	return (void *)(uintptr_t)atomic_load(&v->v);
}

#endif // NNG_HAVE_STDATOMIC

#endif // NNG_PLATFORM_ZEPHYR
