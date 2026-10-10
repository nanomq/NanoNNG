//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr internal pipe for pollq wakeup.
// Zephyr has no pipe()/eventfd. We use a dup of stdin as a dummy
// readable fd; actual wakeup is via 100ms poll() timeout in pollq.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <errno.h>
#include <unistd.h>

int
nni_plat_pipe_open(int *wfd, int *rfd)
{
	// Use stdin (fd 0) as a dummy readable fd for poll().
	// It's always available and never becomes readable in QEMU.
	*wfd = 0;
	*rfd = 0;
	return (0);
}

void
nni_plat_pipe_raise(int wfd)
{
	(void) wfd;
}

void
nni_plat_pipe_clear(int rfd)
{
	(void) rfd;
}

void
nni_plat_pipe_close(int wfd, int rfd)
{
	(void) wfd;
	(void) rfd;
}

#endif // NNG_PLATFORM_ZEPHYR
