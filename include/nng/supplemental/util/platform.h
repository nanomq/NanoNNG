//
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef NNG_SUPPLEMENTAL_UTIL_PLATFORM_H
#define NNG_SUPPLEMENTAL_UTIL_PLATFORM_H

// The declarations in this file are provided to assist with application
// portability.  Conceptually these APIs are based on work we have already
// done for NNG internals, and we find that they are useful in building
// portable applications.

// If it is more natural to use native system APIs like pthreads or C11
// APIs or Windows APIs, then by all means please feel free to simply
// ignore this.

#include <stddef.h>
#include <stdint.h>

#include <nng/nng.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return unix timestamp (milliseconds) .
NNG_DECL nng_time nng_timestamp(void);

// Get current process Id.
NNG_DECL int nng_getpid();

// nng_rwlock is a rwlock. TODO more comments
typedef struct nng_rwlock nng_rwlock;
NNG_DECL int  nng_rwlock_alloc(nng_rwlock **);
NNG_DECL void nng_rwlock_free(nng_rwlock *);
NNG_DECL void nng_rwlock_rdlock(nng_rwlock *);
NNG_DECL void nng_rwlock_rwlock(nng_rwlock *);
NNG_DECL void nng_rwlock_unlock(nng_rwlock *);

//
// Atomics support.  This will evolve over time.
//

// nng_atomic_flag supports only test-and-set and reset operations.
// This can be implemented without locks on any reasonable system, and
// it corresponds to C11 atomic flag.
typedef struct nni_atomic_flag nng_atomic_flag;

NNG_DECL bool nng_atomic_flag_test_and_set(nng_atomic_flag *);
NNG_DECL void nng_atomic_flag_reset(nng_atomic_flag *);

// nng_atomic_bool is for boolean flags that need to be checked without
// changing their value.  This might require a lock on some systems.
typedef struct nni_atomic_bool nng_atomic_bool;

NNG_DECL int  nng_atomic_alloc_bool(nng_atomic_bool **v);
NNG_DECL void nng_atomic_free_bool(nng_atomic_bool *v);
NNG_DECL void nng_atomic_set_bool(nng_atomic_bool *, bool);
NNG_DECL bool nng_atomic_get_bool(nng_atomic_bool *);
NNG_DECL bool nng_atomic_swap_bool(nng_atomic_bool *, bool);

typedef struct nni_atomic_u64 nng_atomic_u64;

NNG_DECL int      nng_atomic_alloc64(nng_atomic_u64 **v);
NNG_DECL void     nng_atomic_free64(nng_atomic_u64 *v);
NNG_DECL void     nng_atomic_add64(nng_atomic_u64 *, uint64_t);
NNG_DECL void     nng_atomic_sub64(nng_atomic_u64 *, uint64_t);
NNG_DECL uint64_t nng_atomic_get64(nng_atomic_u64 *);
NNG_DECL void     nng_atomic_set64(nng_atomic_u64 *, uint64_t);
NNG_DECL uint64_t nng_atomic_swap64(nng_atomic_u64 *, uint64_t);
NNG_DECL uint64_t nng_atomic_dec64_nv(nng_atomic_u64 *);
NNG_DECL void     nng_atomic_inc64(nng_atomic_u64 *);

// nng_atomic_cas64 is a compare and swap.  The second argument is the
// value to compare against, and the third is the new value. Returns
// true if the value was set.
NNG_DECL bool nng_atomic_cas64(nng_atomic_u64 *, uint64_t, uint64_t);

// In a lot of circumstances, we want a simple atomic reference count,
// or atomic tunable values for integers like queue lengths or TTLs.
// These native integer forms should be preferred over the 64 bit versions
// unless larger bit sizes are truly needed.  They will be more efficient
// on many platforms.
typedef struct nni_atomic_int nng_atomic_int;

NNG_DECL int  nng_atomic_alloc(nng_atomic_int **v);
NNG_DECL void nng_atomic_free(nng_atomic_int *v);
NNG_DECL void nng_atomic_add(nng_atomic_int *, int);
NNG_DECL void nng_atomic_sub(nng_atomic_int *, int);
NNG_DECL int  nng_atomic_get(nng_atomic_int *);
NNG_DECL void nng_atomic_set(nng_atomic_int *, int);
NNG_DECL int  nng_atomic_swap(nng_atomic_int *, int);
NNG_DECL int  nng_atomic_dec_nv(nng_atomic_int *);
NNG_DECL void nng_atomic_dec(nng_atomic_int *);
NNG_DECL void nng_atomic_inc(nng_atomic_int *);

// nng_atomic_cas is a compare and swap.  The second argument is the
// value to compare against, and the third is the new value. Returns
// true if the value was set.
NNG_DECL bool nng_atomic_cas(nng_atomic_int *, int, int);

// atomic pointers.  We only support a few operations.
typedef struct nni_atomic_ptr nng_atomic_ptr;
NNG_DECL int                  nng_atomic_alloc_ptr(nng_atomic_ptr **v);
NNG_DECL void                 nng_atomic_free_ptr(nng_atomic_ptr *v);
NNG_DECL void                 nng_atomic_set_ptr(nng_atomic_ptr *, void *);
NNG_DECL void *               nng_atomic_get_ptr(nng_atomic_ptr *);

#ifdef __cplusplus
}
#endif

#endif // NNG_SUPPLEMENTAL_UTIL_PLATFORM_H
