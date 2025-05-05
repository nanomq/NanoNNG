//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// We still use the udp platform layer. And defined a udp dialer that
// is compatible with the nng streams.

#include <stdint.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h> // for endianness functions
#endif

#include <nng/nng.h>

#include "core/nng_impl.h"
#include "core/udp.h"

typedef struct {
	nng_stream_dialer ops;
	char *            host;
	char *            port;
	int               af; // address family
	bool              closed;
	nng_sockaddr      peersa;
	nng_sockaddr      selfsa;
	nni_plat_udp *    u;
	nni_aio *         resaio; // resolver aio
	nni_aio *         conaio; // platform connection aio
	nni_list          conaios;
	nni_mtx           mtx;
} udp_dialer;

static void
udp_send(void *arg, nni_aio *aio)
{
	nni_udp_conn *c = arg;
	udp_dialer *  d = c->d;
	nni_aio_set_input(aio, 0, &d->peersa);
	nni_plat_udp_send(c->u, aio);
}

static void
udp_recv(void *arg, nni_aio *aio)
{
	nni_udp_conn *c = arg;
	nni_plat_udp_recv(c->u, aio);
}

static int
udp_get(void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(buf);
	NNI_ARG_UNUSED(szp);
	NNI_ARG_UNUSED(t);
	return 0;
}

static int
udp_set(void *arg, const char *name, const void *buf, size_t sz, nni_type t)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(buf);
	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);
	return 0;
}

static void
udp_close(void *arg)
{
	NNI_ARG_UNUSED(arg);
	// Nothing to do.
	// nni_udp_conn *c = arg;
	// nni_plat_udp_close(c->u);
}

static void
udp_fini(void *arg)
{
	nni_udp_conn *c = arg;
	udp_close(c);

	NNI_FREE_STRUCT(c);
}
static nni_reap_list udp_reap_list = {
	.rl_offset = offsetof(nni_udp_conn, reap),
	.rl_func   = udp_fini,
};
static void
udp_free(void *arg)
{
	nni_udp_conn *c = arg;
	nni_reap(&udp_reap_list, c);
}

static int
nni_udp_conn_alloc(nni_udp_conn **cp, nni_plat_udp *u, udp_dialer *d)
{
	nni_udp_conn *c;
	if ((c = NNI_ALLOC_STRUCT(c)) == NULL) {
		return NNG_ENOMEM;
	}

	c->u = u;
	c->d = (void *)d;

	c->stream.s_free  = udp_free;
	c->stream.s_close = udp_close;
	c->stream.s_recv  = udp_recv;
	c->stream.s_send  = udp_send;
	c->stream.s_get   = udp_get;
	c->stream.s_set   = udp_set;

	*cp = c;
	return 0;
}

static void
udp_dial_cancel(nni_aio *aio, void *arg, int rv)
{
	udp_dialer *d = arg;

	nni_mtx_lock(&d->mtx);
	if (nni_aio_list_active(aio)) {
		nni_aio_list_remove(aio);
		nni_aio_finish_error(aio, rv);

		if (nni_list_empty(&d->conaios)) {
			nni_aio_abort(d->conaio, NNG_ECANCELED);
			nni_aio_abort(d->resaio, NNG_ECANCELED);
		}
	}
	nni_mtx_unlock(&d->mtx);
}

static uint32_t str2u32ip(char *str)
{
	/*
	uint32_t res;
	if (inet_pton(AF_INET, str, &res) == 1)
		return res;
	return 0;
	*/
	return inet_addr(str);
}

static uint16_t str2port(char *str)
{
	long   port;
	char * end;
	port = strtol(str, &end, 10);
	if (*end == '\0') {
		if ((port < 0) || (port > 0xffff)) {
			return 0;
		}
		return port;
	}
	return 0;
}

static void
udp_dial_start_next(udp_dialer *d)
{
	if (nni_list_empty(&d->conaios)) {
		return;
	}
	// We don't resolv domain... Just assign host to sockaddr directly
	// nni_resolv_ip(d->host, d->port, d->af, false, &d->sa, d->resaio);
	if (d->af == NNG_AF_INET || d->af == NNG_AF_UNSPEC) {
		d->peersa.s_in.sa_family = NNG_AF_INET;
		d->peersa.s_in.sa_port   = htons(str2port(d->port));
		d->peersa.s_in.sa_addr   = str2u32ip(d->host);
	} else if (d->af == NNG_AF_INET6) {
		log_error("Unsupported for INET6.\n");
		printf("Unsupported for INET6.\n");
		return;
	}
	if (nni_aio_begin(d->resaio) != 0) {
		return;
	}
	nni_aio_finish(d->resaio, 0, 0);
}

static void
udp_dial_res_cb(void *arg)
{
	udp_dialer *d = arg;
	nni_aio *   aio;
	int         rv;

	nni_mtx_lock(&d->mtx);
	if (d->closed || ((aio = nni_list_first(&d->conaios)) == NULL)) {
		log_warn("UDP socket has been closed");
		// ignore this.
		while ((aio = nni_list_first(&d->conaios)) != NULL) {
			nni_list_remove(&d->conaios, aio);
			nni_aio_finish_error(aio, NNG_ECLOSED);
		}
		nni_mtx_unlock(&d->mtx);
		return;
	}

	if ((rv = nni_aio_result(d->resaio)) != 0) {
		nni_list_remove(&d->conaios, aio);
		nni_aio_finish_error(aio, rv);

		// try again for next connection...
		udp_dial_start_next(d);

	} else {
		uint8_t *x = (uint8_t *)&(d->selfsa.s_in.sa_addr);
		log_info("rv%d self %d.%d.%d.%d:%d\n", rv,
			x[0], x[1], x[2], x[3], d->selfsa.s_in.sa_port);
		if ((rv = nni_plat_udp_open(&d->u, &d->selfsa)) != 0) {
			nni_aio_finish_error(d->conaio, rv);
		} else {
			nni_udp_conn *c;
			rv = nni_udp_conn_alloc(&c, d->u, d);
			if (rv != 0) {
				nni_aio_finish_error(d->conaio, rv);
			} else {
				nni_aio_set_output(d->conaio, 0, c);
				nni_aio_finish(d->conaio, 0, 0);
			}
		}
	}

	nni_mtx_unlock(&d->mtx);
}

static void
udp_dial_con_cb(void *arg)
{
	udp_dialer *d = arg;
	nng_aio *   aio;
	int         rv;

	nni_mtx_lock(&d->mtx);
	rv = nni_aio_result(d->conaio);
	if ((d->closed) || ((aio = nni_list_first(&d->conaios)) == NULL)) {
		if (rv == 0) {
			// Make sure we discard the underlying connection.
			nng_stream_free(nni_aio_get_output(d->conaio, 0));
			nni_aio_set_output(d->conaio, 0, NULL);
		}
		nni_mtx_unlock(&d->mtx);
		return;
	}
	nni_list_remove(&d->conaios, aio);
	if (rv != 0) {
		nni_aio_finish_error(aio, rv);
	} else {
		nni_aio_set_output(aio, 0, nni_aio_get_output(d->conaio, 0));
		nni_aio_finish(aio, 0, 0);
	}

	udp_dial_start_next(d);
	nni_mtx_unlock(&d->mtx);
}

static void
udp_dialer_close(void *arg)
{
	udp_dialer *d = arg;
	nni_aio *   aio;
	nni_mtx_lock(&d->mtx);
	d->closed = true;
	while ((aio = nni_list_first(&d->conaios)) != NULL) {
		nni_list_remove(&d->conaios, aio);
		nni_aio_finish_error(aio, NNG_ECLOSED);
	}
	nni_plat_udp_close(d->u);
	nni_mtx_unlock(&d->mtx);
}

static void
udp_dialer_free(void *arg)
{
	udp_dialer *d = arg;

	if (d == NULL) {
		return;
	}

	nni_aio_stop(d->resaio);
	nni_aio_stop(d->conaio);
	nni_aio_free(d->resaio);
	nni_aio_free(d->conaio);

	if (d->u != NULL) {
		nni_plat_udp_close(d->u);
	}
	nni_mtx_fini(&d->mtx);
	nni_strfree(d->host);
	nni_strfree(d->port);
	NNI_FREE_STRUCT(d);
}

static void
udp_dialer_dial(void *arg, nng_aio *aio)
{
	udp_dialer *d = arg;
	int         rv;
	if (nni_aio_begin(aio) != 0) {
		return;
	}
	nni_mtx_lock(&d->mtx);
	if (d->closed) {
		nni_mtx_unlock(&d->mtx);
		nni_aio_finish_error(aio, NNG_ECLOSED);
		return;
	}
	if ((rv = nni_aio_schedule(aio, udp_dial_cancel, d)) != 0) {
		nni_mtx_unlock(&d->mtx);
		nni_aio_finish_error(aio, rv);
		return;
	}
	nni_list_append(&d->conaios, aio);
	if (nni_list_first(&d->conaios) == aio) {
		udp_dial_start_next(d);
	}
	nni_mtx_unlock(&d->mtx);
}

static int
udp_dialer_get(void *arg, const char *name, void *buf, size_t *szp, nni_type t)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(buf);
	NNI_ARG_UNUSED(szp);
	NNI_ARG_UNUSED(t);
	return 0;
}

static int
udp_dialer_set(
    void *arg, const char *name, const void *buf, size_t sz, nni_type t)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(buf);
	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);
	return 0;
}

static int
udp_dialer_alloc(udp_dialer **dp)
{
	int         rv;
	udp_dialer *d;

	if ((d = NNI_ALLOC_STRUCT(d)) == NULL) {
		return (NNG_ENOMEM);
	}

	nni_mtx_init(&d->mtx);
	nni_aio_list_init(&d->conaios);

	if (((rv = nni_aio_alloc(&d->resaio, udp_dial_res_cb, d)) != 0) ||
	    ((rv = nni_aio_alloc(&d->conaio, udp_dial_con_cb, d)) != 0)) {
		udp_dialer_free(d);
		return (rv);
	}

	d->u = NULL;

	d->ops.sd_close = udp_dialer_close;
	d->ops.sd_free  = udp_dialer_free;
	d->ops.sd_dial  = udp_dialer_dial;
	d->ops.sd_get   = udp_dialer_get;
	d->ops.sd_set   = udp_dialer_set;

	*dp = d;
	return (0);
}

int
nni_udp_dialer_alloc(nng_stream_dialer **dp, const nng_url *url)
{
	udp_dialer *d;
	int         rv;
	const char *p;

	if ((rv = udp_dialer_alloc(&d)) != 0) {
		return (rv);
	}

	if (((p = url->u_port) == NULL) || (strlen(p) == 0)) {
		return (NNG_EADDRINVAL);
	}

	if ((strlen(p) == 0) || (strlen(url->u_hostname) == 0)) {
		// Dialer needs both a destination hostname and port.
		udp_dialer_free(d);
		return (NNG_EADDRINVAL);
	}

	if (strchr(url->u_scheme, '4') != NULL) {
		d->af = NNG_AF_INET;
	} else if (strchr(url->u_scheme, '6') != NULL) {
		d->af = NNG_AF_INET6;
	} else {
		d->af = NNG_AF_UNSPEC;
	}

	d->selfsa.s_in.sa_family = NNG_AF_INET; // FIXME
	d->selfsa.s_in.sa_addr   = htonl(0x7f000001); // 127.0.0.1
	d->selfsa.s_in.sa_port   = 0; // wild card port binding

	if (((d->host = nng_strdup(url->u_hostname)) == NULL) ||
	    ((d->port = nng_strdup(p)) == NULL)) {
		udp_dialer_free(d);
		return (NNG_ENOMEM);
	}

	*dp = (void *) d;
	return (0);
}

