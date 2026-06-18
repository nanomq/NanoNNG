//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr memory allocation -- standard C malloc/free.
// Zephyr provides malloc/calloc/free when CONFIG_HEAP_MEM_POOL_SIZE > 0.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <stdlib.h>

void *
nni_alloc(size_t sz)
{
	return (sz > 0 ? malloc(sz) : NULL);
}

void *
nni_zalloc(size_t sz)
{
	return (sz > 0 ? calloc(1, sz) : NULL);
}

void
nni_free(void *ptr, size_t size)
{
	NNI_ARG_UNUSED(size);
	free(ptr);
}

#endif // NNG_PLATFORM_ZEPHYR
