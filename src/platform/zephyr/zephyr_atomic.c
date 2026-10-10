//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr atomics.
// Uses C11 stdatomic for 32-bit types; __sync builtins for 64-bit.
// __sync generates inline lock cmpxchg8b even with -mno-mmx -mno-sse,
// unlike C11 stdatomic / __atomic which regress to libatomic calls.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#if NNG_HAVE_STDATOMIC

#include <stdatomic.h>

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

// ---- 64-bit atomics (GCC __sync builtins) ----
// __sync builtins work on plain uint64_t* and emit inline lock cmpxchg8b
// even with -mno-mmx -mno-sse, unlike C11 stdatomic / __atomic builtins.

void
nni_atomic_init64(nni_atomic_u64 *v)
{
	v->v = 0;
}

void
nni_atomic_add64(nni_atomic_u64 *v, uint64_t bump)
{
	(void) __sync_fetch_and_add(&v->v, bump);
}

void
nni_atomic_sub64(nni_atomic_u64 *v, uint64_t bump)
{
	(void) __sync_fetch_and_sub(&v->v, bump);
}

uint64_t
nni_atomic_get64(nni_atomic_u64 *v)
{
	return (__sync_fetch_and_add(&v->v, 0));
}

void
nni_atomic_set64(nni_atomic_u64 *v, uint64_t val)
{
	(void) __sync_lock_test_and_set(&v->v, val);
}

uint64_t
nni_atomic_swap64(nni_atomic_u64 *v, uint64_t val)
{
	return (__sync_lock_test_and_set(&v->v, val));
}

void
nni_atomic_inc64(nni_atomic_u64 *v)
{
	(void) __sync_fetch_and_add(&v->v, 1);
}

uint64_t
nni_atomic_dec64_nv(nni_atomic_u64 *v)
{
	// __sync_sub_and_fetch returns the new value.
	return (__sync_sub_and_fetch(&v->v, 1));
}

bool
nni_atomic_cas64(nni_atomic_u64 *v, uint64_t comp, uint64_t new_v)
{
	return (__sync_bool_compare_and_swap(&v->v, comp, new_v));
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

// ---- fini (no-op for C11 stdatomic) ----

void nni_atomic_fini_flag(nni_atomic_flag *f) { NNI_ARG_UNUSED(f); }
void nni_atomic_fini_bool(nni_atomic_bool *b) { NNI_ARG_UNUSED(b); }
void nni_atomic_fini64(nni_atomic_u64 *v)    { NNI_ARG_UNUSED(v); }
void nni_atomic_fini(nni_atomic_int *v)      { NNI_ARG_UNUSED(v); }
void nni_atomic_fini_ptr(nni_atomic_ptr *v)  { NNI_ARG_UNUSED(v); }

#else // !NNG_HAVE_STDATOMIC — pthread mutex fallback

// On architectures without lock-free 64-bit atomics (e.g., 32-bit ARM
// Cortex-M/R), use pthread mutexes inside each atomic struct.  Slower
// and not ISR-safe, but correct for all platforms.

#include <pthread.h>

#define MUTEX_LOCK(m)   pthread_mutex_lock(m)
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(m)

bool
nni_atomic_flag_test_and_set(nni_atomic_flag *f)
{
	bool old;
	MUTEX_LOCK(&f->m);
	old = f->v;
	f->v = true;
	MUTEX_UNLOCK(&f->m);
	return (old);
}

void
nni_atomic_flag_reset(nni_atomic_flag *f)
{
	MUTEX_LOCK(&f->m);
	f->v = false;
	MUTEX_UNLOCK(&f->m);
}

void
nni_atomic_set_bool(nni_atomic_bool *v, bool b)
{
	MUTEX_LOCK(&v->m);
	v->v = b;
	MUTEX_UNLOCK(&v->m);
}

bool
nni_atomic_get_bool(nni_atomic_bool *v)
{
	bool b;
	MUTEX_LOCK(&v->m);
	b = v->v;
	MUTEX_UNLOCK(&v->m);
	return (b);
}

bool
nni_atomic_swap_bool(nni_atomic_bool *v, bool b)
{
	bool old;
	MUTEX_LOCK(&v->m);
	old = v->v;
	v->v = b;
	MUTEX_UNLOCK(&v->m);
	return (old);
}

void
nni_atomic_init_bool(nni_atomic_bool *v)
{
	v->v = false;
	pthread_mutex_init(&v->m, NULL);
}

void
nni_atomic_init(nni_atomic_int *v)
{
	v->v = 0;
	pthread_mutex_init(&v->m, NULL);
}

void
nni_atomic_add(nni_atomic_int *v, int bump)
{
	MUTEX_LOCK(&v->m);
	v->v += bump;
	MUTEX_UNLOCK(&v->m);
}

void
nni_atomic_sub(nni_atomic_int *v, int bump)
{
	MUTEX_LOCK(&v->m);
	v->v -= bump;
	MUTEX_UNLOCK(&v->m);
}

int
nni_atomic_get(nni_atomic_int *v)
{
	int val;
	MUTEX_LOCK(&v->m);
	val = v->v;
	MUTEX_UNLOCK(&v->m);
	return (val);
}

void
nni_atomic_set(nni_atomic_int *v, int val)
{
	MUTEX_LOCK(&v->m);
	v->v = val;
	MUTEX_UNLOCK(&v->m);
}

int
nni_atomic_swap(nni_atomic_int *v, int val)
{
	int old;
	MUTEX_LOCK(&v->m);
	old = v->v;
	v->v = val;
	MUTEX_UNLOCK(&v->m);
	return (old);
}

void
nni_atomic_inc(nni_atomic_int *v)
{
	MUTEX_LOCK(&v->m);
	v->v++;
	MUTEX_UNLOCK(&v->m);
}

void
nni_atomic_dec(nni_atomic_int *v)
{
	MUTEX_LOCK(&v->m);
	v->v--;
	MUTEX_UNLOCK(&v->m);
}

int
nni_atomic_dec_nv(nni_atomic_int *v)
{
	int nv;
	MUTEX_LOCK(&v->m);
	nv = --v->v;
	MUTEX_UNLOCK(&v->m);
	return (nv);
}

bool
nni_atomic_cas(nni_atomic_int *v, int old, int new)
{
	bool rv;
	MUTEX_LOCK(&v->m);
	if ((rv = (v->v == old))) {
		v->v = new;
	}
	MUTEX_UNLOCK(&v->m);
	return (rv);
}

// ---- 64-bit atomics (pthread mutex) ----

void
nni_atomic_init64(nni_atomic_u64 *v)
{
	v->v = 0;
	pthread_mutex_init(&v->m, NULL);
}

void
nni_atomic_add64(nni_atomic_u64 *v, uint64_t bump)
{
	MUTEX_LOCK(&v->m);
	v->v += bump;
	MUTEX_UNLOCK(&v->m);
}

void
nni_atomic_sub64(nni_atomic_u64 *v, uint64_t bump)
{
	MUTEX_LOCK(&v->m);
	v->v -= bump;
	MUTEX_UNLOCK(&v->m);
}

uint64_t
nni_atomic_get64(nni_atomic_u64 *v)
{
	uint64_t val;
	MUTEX_LOCK(&v->m);
	val = v->v;
	MUTEX_UNLOCK(&v->m);
	return (val);
}

void
nni_atomic_set64(nni_atomic_u64 *v, uint64_t val)
{
	MUTEX_LOCK(&v->m);
	v->v = val;
	MUTEX_UNLOCK(&v->m);
}

uint64_t
nni_atomic_swap64(nni_atomic_u64 *v, uint64_t val)
{
	uint64_t old;
	MUTEX_LOCK(&v->m);
	old = v->v;
	v->v = val;
	MUTEX_UNLOCK(&v->m);
	return (old);
}

void
nni_atomic_inc64(nni_atomic_u64 *v)
{
	MUTEX_LOCK(&v->m);
	v->v++;
	MUTEX_UNLOCK(&v->m);
}

uint64_t
nni_atomic_dec64_nv(nni_atomic_u64 *v)
{
	uint64_t nv;
	MUTEX_LOCK(&v->m);
	nv = --v->v;
	MUTEX_UNLOCK(&v->m);
	return (nv);
}

bool
nni_atomic_cas64(nni_atomic_u64 *v, uint64_t comp, uint64_t new_v)
{
	bool rv;
	MUTEX_LOCK(&v->m);
	if ((rv = (v->v == comp))) {
		v->v = new_v;
	}
	MUTEX_UNLOCK(&v->m);
	return (rv);
}

// ---- Pointer atomics (pthread mutex) ----

void
nni_atomic_set_ptr(nni_atomic_ptr *v, void *p)
{
	MUTEX_LOCK(&v->m);
	v->v = p;
	MUTEX_UNLOCK(&v->m);
}

void *
nni_atomic_get_ptr(nni_atomic_ptr *v)
{
	void *p;
	MUTEX_LOCK(&v->m);
	p = v->v;
	MUTEX_UNLOCK(&v->m);
	return (p);
}

// ---- fini (destroy embedded mutex) ----

void
nni_atomic_fini_flag(nni_atomic_flag *f)
{
	(void) pthread_mutex_destroy(&f->m);
}

void
nni_atomic_fini_bool(nni_atomic_bool *b)
{
	(void) pthread_mutex_destroy(&b->m);
}

void
nni_atomic_fini64(nni_atomic_u64 *v)
{
	(void) pthread_mutex_destroy(&v->m);
}

void
nni_atomic_fini(nni_atomic_int *v)
{
	(void) pthread_mutex_destroy(&v->m);
}

void
nni_atomic_fini_ptr(nni_atomic_ptr *v)
{
	(void) pthread_mutex_destroy(&v->m);
}

#endif // NNG_HAVE_STDATOMIC

#endif // NNG_PLATFORM_ZEPHYR
