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

#ifdef NNG_HAVE_STDATOMIC

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

#endif // NNG_HAVE_STDATOMIC

#endif // NNG_PLATFORM_ZEPHYR
