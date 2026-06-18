//
// Copyright 2025 EMQX. All rights reserved.
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef PLATFORM_ZEPHYR_CONFIG_H
#define PLATFORM_ZEPHYR_CONFIG_H

// Zephyr RTOS platform configuration.
//
// We assert fixed choices here because Zephyr cross-compilation
// makes configure-time probes unreliable.

#ifdef NNG_PLATFORM_ZEPHYR

// Clock: prefer CLOCK_MONOTONIC; fall back to CLOCK_REALTIME
#ifndef NNG_USE_CLOCKID
#if defined(CLOCK_MONOTONIC)
#define NNG_USE_CLOCKID CLOCK_MONOTONIC
#elif defined(CLOCK_REALTIME)
#define NNG_USE_CLOCKID CLOCK_REALTIME
#else
#define NNG_USE_GETTIMEOFDAY
#endif
#endif

// Random: /dev/urandom is the safest fallback on Zephyr
#ifndef NNG_USE_DEVURANDOM
#define NNG_USE_DEVURANDOM 1
#endif

// DNS resolution via getaddrinfo (Zephyr supports it with CONFIG_DNS_RESOLVER)
#define NNG_USE_POSIX_RESOLV_GAI 1

#endif // NNG_PLATFORM_ZEPHYR

#endif // PLATFORM_ZEPHYR_CONFIG_H
