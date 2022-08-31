//
// Copyright 2020 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
#include "nng/supplemental/nanolib/cmd.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <string.h>

char *cmd_output_buff = NULL;
int   cmd_output_len  = 0;

#ifndef NNG_PLATFORM_WINDOWS
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

int
nano_cmd_run_status(const char *cmd)
{
	int          error, pipes[2], stderr_fd = -1, ret = 0;
	unsigned int sock_opts;

	log_info("cmd = %s", cmd);

	/* -------------------
	 * validate given args
	 * ---------------- */
	if (cmd == NULL)
	  return (-1);
	
	if (!cmd_output_buff)
		cmd_output_buff = malloc(CMD_BUFF_LEN);

	if (!cmd_output_buff)
		return -1;

	error = pipe(pipes);
	if (error < 0) {
		log_warn("could not create a pipe to '%s': %s", cmd,
		    strerror(errno));
		return -1;
	}

	/* save stderr */
	stderr_fd = dup(STDERR_FILENO);

	/* connect the commands output with the pipe for later logging */
	dup2(pipes[1], STDERR_FILENO);
	close(pipes[1]);

	error = system(cmd);

	/* copy stderr back */
	dup2(stderr_fd, STDERR_FILENO);
	close(stderr_fd);

	memset(cmd_output_buff, 0, CMD_BUFF_LEN);
	sock_opts = fcntl(pipes[0], F_GETFL, 0);
	fcntl(pipes[0], F_SETFL, sock_opts | O_NONBLOCK);
	cmd_output_len = read(pipes[0], cmd_output_buff, CMD_BUFF_LEN);

	if (error < 0)
		ret = error;
	else if (WIFEXITED(error) && WEXITSTATUS(error) != 0)
		ret = WEXITSTATUS(error);

	close(pipes[0]);
	return ret;
}

int
nano_cmd_run(const char *cmd)
{
	int error, ret = 0;

	error = nano_cmd_run_status(cmd);

	if (error != 0) {
		log_warn("command '%s' returned an error", cmd);

		if (cmd_output_len > 0)
			log_debug("          %s", cmd_output_buff);
		ret = -1;
	}

	return ret;
}

int
nano_cmd_frun(const char *format, ...)
{
	va_list args;
	char *  cmd;
	int     ret;

	cmd = malloc(PATH_MAX);
	if (!cmd)
		return -1;

	va_start(args, format);
	/* --------------------------------------
	 * vsnprintf() to prevent buffer overflow
	 * ----------------------------------- */
	vsnprintf(cmd, PATH_MAX-1, format, args);
	va_end(args);

	ret = nano_cmd_run(cmd);
	free(cmd);
	return ret;
}
#endif

void
nano_cmd_cleanup(void)
{
	if (!cmd_output_buff)
		return;

	free(cmd_output_buff);
	cmd_output_buff = NULL;
}
