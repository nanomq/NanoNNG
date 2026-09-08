//
// Copyright 2025 EMQX. All rights reserved.
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef PLATFORM_ZEPHYR_POLLQ_H
#define PLATFORM_ZEPHYR_POLLQ_H

// Pollq subsystem -- Zephyr uses poll() only (no epoll/kqueue/eventfd).
// Compatible with posix pollq definitions.

#ifdef NNG_PLATFORM_ZEPHYR

#include <poll.h>

// We use the same pollq structures as POSIX.
typedef struct nni_posix_pfd     nni_posix_pfd;
typedef struct nni_posix_pollq   nni_posix_pollq;
typedef void (*nni_posix_pfd_cb)(nni_posix_pfd *, unsigned, void *);

extern int  nni_posix_pollq_sysinit(void);
extern void nni_posix_pollq_sysfini(void);

extern int  nni_posix_pfd_init(nni_posix_pfd **, int);
extern void nni_posix_pfd_fini(nni_posix_pfd *);
extern void nni_posix_pfd_close(nni_posix_pfd *);
extern int  nni_posix_pfd_fd(nni_posix_pfd *);
extern void nni_posix_pfd_set_cb(nni_posix_pfd *, nni_posix_pfd_cb, void *);
extern int  nni_posix_pfd_arm(nni_posix_pfd *, unsigned);

// Use same values as posix_pollq.h
#define NNI_POLL_IN   ((unsigned) POLLIN)
#define NNI_POLL_OUT  ((unsigned) POLLOUT)
#define NNI_POLL_HUP  ((unsigned) POLLHUP)
#define NNI_POLL_ERR  ((unsigned) POLLERR)
#define NNI_POLL_INVAL ((unsigned) POLLNVAL)

// Pipe operations for internal wakeup (no eventfd on Zephyr)
extern int  nni_plat_pipe_open(int *wfd, int *rfd);
extern void nni_plat_pipe_raise(int wfd);
extern void nni_plat_pipe_clear(int rfd);
extern void nni_plat_pipe_close(int wfd, int rfd);

#endif // NNG_PLATFORM_ZEPHYR

#endif // PLATFORM_ZEPHYR_POLLQ_H
