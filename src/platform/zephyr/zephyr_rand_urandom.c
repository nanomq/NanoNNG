//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr random number generator.
//
// nni_random() is documented as returning a cryptographically secure value
// (core/platform.h), so it is served by Zephyr's CSPRNG whenever the build
// has one.  That needs an entropy source: the ESP32-S3 has a hardware TRNG
// (zephyr,entropy = &trng0), which an application enables by selecting
// CONFIG_CSPRNG_NEEDED.  Targets with no entropy device -- qemu_x86 has
// none -- keep the clearly-labelled non-secure fallback at the bottom.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#if defined(CONFIG_CSPRNG_ENABLED)
#include <zephyr/random/random.h>

uint32_t
nni_random(void)
{
	uint32_t val;

	if (sys_csrand_get(&val, sizeof(val)) != 0) {
		nni_panic("sys_csrand_get failed");
	}
	return (val);
}

void
nni_plat_seed_prng(void *buf, size_t bufsz)
{
	if (sys_csrand_get(buf, bufsz) != 0) {
		nni_panic("sys_csrand_get failed");
	}
}

#else // !CONFIG_CSPRNG_ENABLED

// No entropy device in this build, so there is no CSPRNG to draw from.
// This is a plain xorshift32 seeded from the clock: adequate for the
// identifiers and retry jitter nng uses it for, and NOT suitable for
// anything secret.  A Zephyr application that handles secrets must enable
// an entropy source so that CONFIG_CSPRNG_ENABLED becomes set.

// Simple PRNG state seeded by clock
static uint32_t zephyr_prng_state = 0;
static int      zephyr_prng_init  = 0;

static void
zephyr_prng_seed(void)
{
	uint64_t now = nni_clock();
	zephyr_prng_state = (uint32_t)(now ^ (now >> 32));
	zephyr_prng_init  = 1;
}

// xorshift32 PRNG
static uint32_t
zephyr_prng_next(void)
{
	if (!zephyr_prng_init) {
		zephyr_prng_seed();
	}
	uint32_t x = zephyr_prng_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	zephyr_prng_state = x;
	return x;
}

uint32_t
nni_random(void)
{
	return zephyr_prng_next();
}

#ifdef NNG_USE_DEVURANDOM

void
nni_plat_seed_prng(void *buf, size_t bufsz)
{
	int fd;
	int n;

	// Try /dev/urandom first
#ifdef O_CLOEXEC
	if ((fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC)) >= 0) {
#else
	if ((fd = open("/dev/urandom", O_RDONLY)) >= 0) {
#endif
		n = (int) read(fd, buf, bufsz);
		(void) close(fd);
		if (n == (int) bufsz) {
			return;
		}
	}

	// Last resort: fill with sequential bytes seeded by clock
	// (better than nothing -- used only for initial PRNG seed)
	uint64_t now = nni_clock();
	for (size_t i = 0; i < bufsz; i++) {
		((uint8_t *) buf)[i] = (uint8_t) ((now + i) & 0xff);
	}
}

#else

void
nni_plat_seed_prng(void *buf, size_t bufsz)
{
	// Fallback: clock-based seed
	uint64_t now = nni_clock();
	for (size_t i = 0; i < bufsz; i++) {
		((uint8_t *) buf)[i] = (uint8_t)((now + i) & 0xff);
	}
}

#endif // NNG_USE_DEVURANDOM

#endif // CONFIG_CSPRNG_ENABLED

#endif // NNG_PLATFORM_ZEPHYR
