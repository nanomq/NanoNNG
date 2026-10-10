//
// Copyright 2025 EMQX. All rights reserved.
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef PLATFORM_ZEPHYR_IMPL_H
#define PLATFORM_ZEPHYR_IMPL_H

#ifdef NNG_PLATFORM_ZEPHYR
#define NNG_PLATFORM_POSIX_ALLOC
#define NNG_PLATFORM_POSIX_DEBUG
#define NNG_PLATFORM_POSIX_CLOCK
#define NNG_PLATFORM_POSIX_TCP
#define NNG_PLATFORM_POSIX_PIPE
#define NNG_PLATFORM_POSIX_RANDOM
#define NNG_PLATFORM_POSIX_SOCKET
#define NNG_PLATFORM_POSIX_THREAD
#define NNG_PLATFORM_POSIX_SOCKADDR
#define NNG_PLATFORM_POSIX_UDP
#include "platform/zephyr/zephyr_config.h"
#endif

#ifdef NNG_PLATFORM_POSIX_SOCKADDR
struct sockaddr;
extern int    nni_posix_sockaddr2nn(nni_sockaddr *, const void *, size_t);
extern size_t nni_posix_nn2sockaddr(void *, const nni_sockaddr *);
#endif

#ifdef NNG_PLATFORM_POSIX_DEBUG
extern int nni_plat_errno(int);
#endif

#ifdef NNG_PLATFORM_POSIX_THREAD
#include <stdint.h>

// When building within Zephyr (__ZEPHYR__ defined), use Zephyr's real
// pthread.h from the POSIX layer.  When building standalone (no Zephyr
// headers available), provide inline minimal stubs.
#if defined(__ZEPHYR__)
#include <pthread.h>
#else
typedef uintptr_t pthread_t;
typedef struct { int __a; } pthread_attr_t;
typedef struct { int __m; } pthread_mutex_t;
typedef struct { int __ma; } pthread_mutexattr_t;
typedef struct { int __c; } pthread_cond_t;
typedef struct { int __ca; } pthread_condattr_t;
typedef struct { int __r; } pthread_rwlock_t;
#endif

#ifndef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER {0}
#endif
#ifndef PTHREAD_COND_INITIALIZER
#define PTHREAD_COND_INITIALIZER {0}
#endif
#ifndef PTHREAD_RWLOCK_INITIALIZER
#define PTHREAD_RWLOCK_INITIALIZER {0}
#endif
#define PTHREAD_MUTEX_ERRORCHECK 2

struct nni_plat_mtx { pthread_mutex_t mtx; };
#define NNI_MTX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }

struct nni_rwlock { pthread_rwlock_t rwl; };
#define NNI_RWLOCK_INITIALIZER { PTHREAD_RWLOCK_INITIALIZER }

struct nni_plat_cv { pthread_cond_t cv; nni_plat_mtx *mtx; };
#define NNI_CV_INITIALIZER(mxp) { .mtx = mxp, .cv = PTHREAD_COND_INITIALIZER }

struct nni_plat_thr { pthread_t tid; void (*func)(void *); void *arg; };
struct nni_plat_flock { int fd; };
#define NNG_PLATFORM_DIR_SEP "/"

#if NNG_HAVE_STDATOMIC
#include <stdatomic.h>
struct nni_atomic_flag { atomic_flag f; };
struct nni_atomic_int  { atomic_int v; };
struct nni_atomic_bool { atomic_bool v; };
struct nni_atomic_ptr  { atomic_uintptr_t v; };
struct nni_atomic_u64  { uint64_t v; };
#else
// Interface the atomics through an embedded mutex.  On Zephyr that has to
// be a k_mutex rather than a pthread one: pthread_mutex_init() allocates
// from a fixed pool (posix_mutex_pool via sys_bitarray_alloc in the POSIX
// layer's mutex.c) and returns ENOMEM once it is exhausted, while nothing
// in the tree calls the nni_atomic_fini*() hooks that would hand a slot
// back -- so a long-lived process would consume one pooled mutex for every
// atomic it ever creates and never return it.  k_mutex is embedded, needs
// no pool, and has nothing to release.
#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#define NNI_ATOMIC_MUTEX_T         struct k_mutex
#define NNI_ATOMIC_MUTEX_INIT(m)   ((void) k_mutex_init(m))
#define NNI_ATOMIC_MUTEX_LOCK(m)   ((void) k_mutex_lock(m, K_FOREVER))
#define NNI_ATOMIC_MUTEX_UNLOCK(m) ((void) k_mutex_unlock(m))
#define NNI_ATOMIC_MUTEX_FINI(m)   ((void) 0)
#else
#define NNI_ATOMIC_MUTEX_T         pthread_mutex_t
#define NNI_ATOMIC_MUTEX_INIT(m)   ((void) pthread_mutex_init(m, NULL))
#define NNI_ATOMIC_MUTEX_LOCK(m)   ((void) pthread_mutex_lock(m))
#define NNI_ATOMIC_MUTEX_UNLOCK(m) ((void) pthread_mutex_unlock(m))
#define NNI_ATOMIC_MUTEX_FINI(m)   ((void) pthread_mutex_destroy(m))
#endif

struct nni_atomic_flag { bool v; NNI_ATOMIC_MUTEX_T m; };
struct nni_atomic_bool { bool v; NNI_ATOMIC_MUTEX_T m; };
struct nni_atomic_int  { int v; NNI_ATOMIC_MUTEX_T m; };
struct nni_atomic_u64  { uint64_t v; NNI_ATOMIC_MUTEX_T m; };
struct nni_atomic_ptr  { void *v; NNI_ATOMIC_MUTEX_T m; };
#endif

#endif // NNG_PLATFORM_POSIX_THREAD

extern int  nni_posix_pollq_sysinit(void);
extern void nni_posix_pollq_sysfini(void);
extern int  nni_posix_resolv_sysinit(void);
extern void nni_posix_resolv_sysfini(void);
extern int  nni_posix_peerid(int fd, uint64_t *euid, uint64_t *egid, uint64_t *prid, uint64_t *znid);

#endif
