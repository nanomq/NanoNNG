//
// Copyright 2022 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
// Copyright 2020 Lager Data, Inc. <support@lagerdata.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/options.h>
#include <nng/supplemental/util/platform.h>

void *       data      = NULL;
size_t       datalen   = 0;
int          async     = 0;

static void
fatal(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

struct addr {
	struct addr *next;
	char *       val;
};

struct addr **
addaddr(struct addr **endp, const char *a)
{
	struct addr *na;

	if (((na = malloc(sizeof(*na))) == NULL) ||
	    ((na->val = malloc(strlen(a) + 1)) == NULL)) {
		fatal("Out of memory.");
	}
	memcpy(na->val, a, strlen(a) + 1);
	na->next = NULL;
	*endp    = na;
	return (&na->next);
}

#define QUERY_EOF "C60="

void
sendrecv(nng_socket sock)
{
	if (data == NULL) {
		fatal("No data to send (specify with --data or --file)");
	}
	int          rv;
	nng_msg *    msg;

	if (((rv = nng_msg_alloc(&msg, 0)) != 0) ||
	    ((rv = nng_msg_append(msg, data, datalen)) != 0)) {
		fatal("%s", nng_strerror(rv));
	}

	if ((rv = nng_sendmsg(sock, msg, 0)) != 0) {
		fatal("Send error: %s", nng_strerror(rv));
	}


	while (true) {
		rv = nng_recvmsg(sock, &msg, 0);
		switch (rv) {
		case 0:
			if (nng_msg_len(msg) == strlen(QUERY_EOF)) {
				if (strncmp(nng_msg_body(msg), QUERY_EOF, strlen(QUERY_EOF)) == 0) {
					nng_msg_free(msg);
					return;
				}
			}
			/* handle reply msg */
			printf("Received %d bytes\n", nng_msg_len(msg));
			nng_msg_free(msg);
			break;
		case NNG_ETIMEDOUT:
		case NNG_ESTATE:
			// We're done receiving
			break;
		default:
			fatal("Cannot receive: %s", nng_strerror(rv));
			break;
		}
	}
}

int
main(int ac, char **av)
{
	int            rv;
	char           scratch[512];
	struct addr *  addrs = NULL;
	struct addr ** addrend;
	nng_socket     sock;
	int            port;

	addrend  = &addrs;

	if (ac != 2 || av[1] == NULL) {
		fatal("No command specified.");
	}

	if ((data = malloc(strlen(av[1]) + 1)) == NULL) {
		fatal("Out of memory.");
	}
	memcpy(data, av[1], strlen(av[1]) + 1);
	datalen = strlen(av[1]);

	port = 10000;
	snprintf(scratch, sizeof(scratch),
	    "tcp://127.0.0.1:%d", port);
	addrend = addaddr(addrend, scratch);

	if (addrs == NULL) {
		fatal("No address specified.");
	}

	rv = nng_pair0_open(&sock);

	if (rv != 0) {
		fatal("Unable to open socket: %s", nng_strerror(rv));
	}

	struct addr *a = addrs;
	nng_dialer      d;
	rv = nng_dialer_create(&d, sock, a->val);
	if (rv != 0) {
		fatal("Unable to create dialer for %s: %s",
		    a->val, nng_strerror(rv));
	}
	rv  = nng_dialer_start(d, async);
	if (rv == 0) {
		char   ustr[256];
		size_t sz;
		sz = sizeof(ustr);
		if (nng_dialer_get(
		        d, NNG_OPT_URL, ustr, &sz) == 0) {
			printf("Connected to: %s\n", ustr);
		}
	}
	if (rv != 0) {
		fatal("Unable to dial on %s: %s", a->val, nng_strerror(rv));
	}
	sendrecv(sock);

	free(data);
	exit(0);
}
