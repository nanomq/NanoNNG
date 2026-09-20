//
// Copyright 2025 EMQX. All rights reserved.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr internal pipe for pollq wakeup.
// Zephyr has no pipe()/eventfd, so there is no wakeup descriptor to hand
// out at all -- the pollq paces itself with its own timeout instead.  See
// nni_plat_pipe_open() below for why fd 0 is not a usable stand-in.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <errno.h>
#include <unistd.h>

int
nni_plat_pipe_open(int *wfd, int *rfd)
{
	// Zephyr's poll() ignores negative descriptors ("Per POSIX,
	// negative fd's are just ignored", lib/os/zvfs/zvfs_poll.c), so
	// register nothing at all: the pollq already paces itself with its
	// own timeout because there is no usable wakeup pipe here.
	//
	// Deliberately not fd 0: a readable stdin (console input on the
	// board) would make poll() return immediately on every pass and
	// spin the poll thread, since nni_plat_pipe_clear() cannot drain it.
	*wfd = -1;
	*rfd = -1;
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
