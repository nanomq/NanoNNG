//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nuts.h>

static const char *udp_test_url  = "udp://13.49.223.253:14567";
static const char *udp_test_url2 = "udp://127.0.0.1:8";

void
test_udp_conn_refused(void)
{
	nng_stream_dialer *dialer;
	nng_aio *          aio;

	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 5000); // 5 sec

	// port 8 is generally not used for anything.
	NUTS_PASS(nng_stream_dialer_alloc(&dialer, udp_test_url2));
	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	NUTS_FAIL(nng_aio_result(aio), NNG_ECONNREFUSED);

	nng_aio_free(aio);
	nng_stream_dialer_free(dialer);
}

TEST_LIST = {
	{ "udp conn refused", test_udp_conn_refused },
	{ NULL, NULL },
};

