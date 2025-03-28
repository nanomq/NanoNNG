//
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdlib.h>
#include <string.h>

#include "core/nng_impl.h"
#include "nng/supplemental/util/platform.h"

nng_time
nng_timestamp(void)
{
	return (nni_timestamp());
}

int
nng_getpid()
{
	return nni_plat_getpid();
}

struct nng_mtx {
	nni_mtx m;
};

struct nng_rwlock {
	nni_rwlock l;
};

int
nng_rwlock_alloc(nng_rwlock **lpp)
{
	nng_rwlock *lp;

	if ((lp = NNI_ALLOC_STRUCT(lp)) == NULL) {
		return (NNG_ENOMEM);
	}
	nni_rwlock_init(&lp->l);
	*lpp = lp;
	return (0);
}

void
nng_rwlock_free(nng_rwlock *lp)
{
	if (lp != NULL) {
		nni_rwlock_fini(&lp->l);
		NNI_FREE_STRUCT(lp);
	}
}

void
nng_rwlock_rdlock(nng_rwlock *lp)
{
	nni_rwlock_rdlock(&lp->l);
}

void
nng_rwlock_wrlock(nng_rwlock *lp)
{
	nni_rwlock_wrlock(&lp->l);
}

void
nng_rwlock_unlock(nng_rwlock *lp)
{
	nni_rwlock_unlock(&lp->l);
}

struct nng_cv {
	nni_cv c;
};

// Added by NanoMQ/NanoNNG
int
nng_atomic_alloc_bool(nng_atomic_bool **b)
{
	if ((*b = nng_alloc(sizeof(nng_atomic_bool))) == NULL) {
		return NNG_ENOMEM;
	}
	nni_atomic_init_bool(*b);

	return (0);
}

void
nng_atomic_free_bool(nng_atomic_bool *b)
{
	nng_free(b, sizeof(nng_atomic_bool));
}

void
nng_atomic_init_bool(nng_atomic_bool *b)
{
	nni_atomic_init_bool(b);
}

void
nng_atomic_set_bool(nng_atomic_bool *b, bool n)
{
	nni_atomic_set_bool(b, n);
}

bool
nng_atomic_get_bool(nng_atomic_bool *b)
{
	return nni_atomic_get_bool(b);
}

bool
nng_atomic_swap_bool(nng_atomic_bool *b, bool n)
{
	return nni_atomic_swap_bool(b, n);
}

int
nng_atomic_alloc64(nng_atomic_u64 **v)
{
	if ((*v = nng_alloc(sizeof(nng_atomic_u64))) == NULL) {
		return NNG_ENOMEM;
	}
	nni_atomic_init64(*v);

	return (0);
}

void
nng_atomic_free64(nng_atomic_u64 *v)
{
	nng_free(v, sizeof(nng_atomic_u64));
}

void
nng_atomic_init64(nng_atomic_u64 *v)
{
	nni_atomic_init64(v);
}

void
nng_atomic_add64(nng_atomic_u64 *v, uint64_t bump)
{
	nni_atomic_add64(v, bump);
}

void
nng_atomic_sub64(nng_atomic_u64 *v, uint64_t bump)
{
	nni_atomic_sub64(v, bump);
}

uint64_t
nng_atomic_get64(nng_atomic_u64 *v)
{
	return nni_atomic_get64(v);
}

void
nng_atomic_set64(nng_atomic_u64 *v, uint64_t u)
{
	nni_atomic_set64(v, u);
}

uint64_t
nng_atomic_swap64(nng_atomic_u64 *v, uint64_t u)
{
	return nni_atomic_swap64(v, u);
}

uint64_t
nng_atomic_dec64_nv(nng_atomic_u64 *v)
{
	return nni_atomic_dec64_nv(v);
}

void
nng_atomic_inc64(nng_atomic_u64 *v)
{
	nni_atomic_inc64(v);
}

bool
nng_atomic_cas64(nng_atomic_u64 *v, uint64_t comp, uint64_t new)
{
	return nni_atomic_cas64(v, comp, new);
}

int
nng_atomic_alloc(nng_atomic_int **v)
{
	if ((*v = nng_alloc(sizeof(nng_atomic_int))) == NULL) {
		return NNG_ENOMEM;
	}
	nni_atomic_init(*v);
	return (0);
}

void
nng_atomic_free(nng_atomic_int *v)
{
	nng_free(v, sizeof(nng_atomic_int));
}

void
nng_atomic_init(nng_atomic_int *v)
{
	nni_atomic_init(v);
}
void
nng_atomic_add(nng_atomic_int *v, int bump)
{
	nni_atomic_add(v, bump);
}

void
nng_atomic_sub(nng_atomic_int *v, int bump)
{
	nni_atomic_sub(v, bump);
}

int
nng_atomic_get(nng_atomic_int *v)
{
	return nni_atomic_get(v);
}

void
nng_atomic_set(nng_atomic_int *v, int i)
{
	nni_atomic_set(v, i);
}

int
nng_atomic_swap(nng_atomic_int *v, int i)
{
	return nni_atomic_swap(v, i);
}

int
nng_atomic_dec_nv(nng_atomic_int *v)
{
	return nni_atomic_dec_nv(v);
}

void
nng_atomic_dec(nng_atomic_int *v)
{
	nni_atomic_dec(v);
}

void
nng_atomic_inc(nng_atomic_int *v)
{
	nni_atomic_inc(v);
}

bool
nng_atomic_cas(nng_atomic_int *v, int comp, int new)
{
	return nni_atomic_cas(v, comp, new);
}

int
nng_atomic_alloc_ptr(nng_atomic_ptr **v)
{
	if ((*v = nng_alloc(sizeof(nng_atomic_ptr))) == NULL) {
		return NNG_ENOMEM;
	}
	return (0);
}

void
nng_atomic_free_ptr(nng_atomic_ptr *v)
{
	nng_free(v, sizeof(nng_atomic_ptr));
}

void
nng_atomic_set_ptr(nng_atomic_ptr *v, void *p)
{
	nni_atomic_set_ptr(v, p);
}

void *
nng_atomic_get_ptr(nng_atomic_ptr *v)
{
	return nni_atomic_get_ptr(v);
}
