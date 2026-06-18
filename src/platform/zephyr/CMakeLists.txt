#
# Copyright 2024 Staysail Systems, Inc. <info@staystail.tech>
# Copyright 2025 EMQX. All rights reserved.
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

# Zephyr RTOS platform.
#
# Zephyr provides a POSIX compatibility layer (CONFIG_POSIX_API) that
# includes pthreads and BSD sockets.  However, it lacks:
#   - eventfd / epoll / kqueue  →  force poll() fallback
#   - AF_UNIX (Unix sockets)    →  disable IPC transport
#   - socketpair(AF_UNIX)       →  use pipe() for internal wakeup
#
# Unlike the POSIX CMakeLists.txt, we do NOT use nng_check_* probes
# because they produce wrong results during Zephyr cross-compilation.
# Instead, we directly assert what Zephyr provides.

if (NNG_PLATFORM_ZEPHYR)
    nng_directory(zephyr)

    # ---- compiler feature macros ----
    nng_defines(_GNU_SOURCE)
    nng_defines(_REENTRANT)
    nng_defines(_THREAD_SAFE)
    nng_defines(_POSIX_PTHREAD_SEMANTICS)

    # ---- feature assertions (no probes -- Zephyr cross-compile safe) ----

    # Zephyr POSIX layer provides pthreads
    nng_defines(NNG_HAVE_SEMAPHORE_PTHREAD)

    # Clock: Zephyr POSIX has clock_gettime on most architectures
    nng_defines(NNG_HAVE_CLOCK_GETTIME)

    # Poll: Zephyr supports poll() via CONFIG_POLL
    nng_defines(NNG_HAVE_POLL)

    # Zephyr has <stdatomic.h> via its GCC/Clang toolchain
    nng_defines(NNG_HAVE_STDATOMIC)

    # Random: use /dev/urandom (simplest, works everywhere)
    nng_defines(NNG_USE_DEVURANDOM)

    # ---- platform source files ----
    nng_sources(
        zephyr_impl.h
        zephyr_config.h
        zephyr_pollq.h

        zephyr_alloc.c
        zephyr_atomic.c
        zephyr_clock.c
        zephyr_debug.c
        zephyr_file.c
        zephyr_peerid.c
        zephyr_pipe.c
        zephyr_pollq_poll.c
        zephyr_rand_urandom.c
        zephyr_resolv_gai.c
        zephyr_sockaddr.c
        zephyr_sockfd.c
        zephyr_socketpair.c
        zephyr_tcpconn.c
        zephyr_tcpdial.c
        zephyr_tcplisten.c
        zephyr_thread.c
        zephyr_udp.c
    )

    # Zephyr pthread is provided by the OS, no need for -lpthread or -latomic
    # (Zephyr's toolchain links these implicitly when CONFIG_POSIX_API=y.)

endif()
