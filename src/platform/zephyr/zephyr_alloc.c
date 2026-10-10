//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr memory allocation.
//
// Default: standard C malloc/calloc/free (Zephyr provides these when
// CONFIG_HEAP_MEM_POOL_SIZE > 0) — i.e. the libc heap, which on
// ESP32-S3 is bounded by the ~512 KB internal SRAM.
//
// With NNG_ZEPHYR_ALLOC_SMH (defined by the demo/nanomq_zephyr_esp32s3
// CMakeLists), allocations come from the external PSRAM window the linker
// reserves inside .ext_ram.data (sized by ESP_SPIRAM_HEAP_SIZE).  The
// NanoMQ broker needs this: its data plane (nng pipes, nanolib db, MQTT
// messages) is several MB at the qemu demo scale and does not fit
// internal SRAM.
//
// The window is managed with a plain struct k_heap, NOT with Zephyr's
// shared multi-heap (shared_multi_heap_alloc/free): those are plain
// sys_heap calls with no lock, while k_heap serializes internally — nng
// allocates from many threads (poller, taskq workers, per-connection
// aios) and unsynchronized smh access corrupts the PSRAM heap under
// concurrent traffic.  soc/espressif's esp_psram.c also registers the
// window into its smh pool; that pool stays empty and unused here.
//
// (The qemu demo's libc malloc has its own lock, so the malloc branch
// below needs none.)

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#ifdef NNG_ZEPHYR_ALLOC_SMH
#include <string.h>
#include <zephyr/kernel.h>

/* Defined by the esp32s3 linker script around the .ext_ram.data heap
 * window (esp_psram.c externs the same symbols). */
extern char _ext_ram_heap_start[];
extern char _ext_ram_heap_end[];

static struct k_heap    nng_psram_heap;
static bool             nng_psram_heap_ready;
static struct k_spinlock nng_psram_heap_init_lock;

static void
nng_psram_heap_ensure(void)
{
	k_spinlock_key_t key = k_spin_lock(&nng_psram_heap_init_lock);

	if (!nng_psram_heap_ready) {
		k_heap_init(&nng_psram_heap, _ext_ram_heap_start,
		    (size_t) (_ext_ram_heap_end - _ext_ram_heap_start));
		nng_psram_heap_ready = true;
	}
	k_spin_unlock(&nng_psram_heap_init_lock, key);
}

void *
nni_alloc(size_t sz)
{
	void *p = NULL;

	if (sz > 0) {
		nng_psram_heap_ensure();
		p = k_heap_aligned_alloc(&nng_psram_heap, 8, sz, K_FOREVER);
	}
	return (p);
}

void *
nni_zalloc(size_t sz)
{
	void *p = NULL;

	if (sz > 0) {
		nng_psram_heap_ensure();
		p = k_heap_aligned_alloc(&nng_psram_heap, 8, sz, K_FOREVER);
		if (p != NULL) {
			memset(p, 0, sz);
		}
	}
	return (p);
}

void
nni_free(void *ptr, size_t size)
{
	NNI_ARG_UNUSED(size);
	if (ptr != NULL) {
		k_heap_free(&nng_psram_heap, ptr);
	}
}

#else /* !NNG_ZEPHYR_ALLOC_SMH */

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

#endif /* NNG_ZEPHYR_ALLOC_SMH */

#endif // NNG_PLATFORM_ZEPHYR
