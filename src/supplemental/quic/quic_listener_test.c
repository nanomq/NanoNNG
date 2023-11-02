//
// Copyright 2023 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nuts.h>

void
test_quic_echo(void)
{
	nng_stream_listener *l;
	nng_stream_dialer *  d;
	nng_aio *            aiod;
	nng_aio *            aiol;
	nng_stream *         sd;
	nng_stream *         sl;
	void *               td;
	void *               tl;
	uint8_t *            bufr;
	uint8_t *            bufs;
	size_t               size = 450001;

	// allocate messages
	NUTS_ASSERT((bufs = nng_alloc(size)) != NULL);
	NUTS_ASSERT((bufr = nng_alloc(size)) != NULL);
	for (size_t i = 0; i < size; i++) {
		bufs[i] = rand() & 0xff;
	}

	NUTS_PASS(nng_aio_alloc(&aiod, NULL, NULL));
	NUTS_PASS(nng_aio_alloc(&aiol, NULL, NULL));
	nng_aio_set_timeout(aiod, 5000); // 5 sec
	nng_aio_set_timeout(aiol, 5000); // 5 sec

	NUTS_PASS(nng_stream_listener_alloc(&l, "quic://127.0.0.1:14567"));
	NUTS_PASS(nng_stream_listener_listen(l));

	NUTS_PASS(nng_stream_dialer_alloc(&d, "quic://127.0.0.1:14567"));

	nng_stream_listener_accept(l, aiol);
	nng_stream_dialer_dial(d, aiod);

	nng_aio_wait(aiol);
	nng_aio_wait(aiod);
	NUTS_PASS(nng_aio_result(aiol));
	NUTS_PASS(nng_aio_result(aiod));

	NUTS_TRUE((sl = nng_aio_get_output(aiol, 0)) != NULL);
	NUTS_TRUE((sd = nng_aio_get_output(aiod, 0)) != NULL);

	td = nuts_stream_send_start(sd, bufs, size);
	tl = nuts_stream_recv_start(sl, bufr, size);

	NUTS_PASS(nuts_stream_wait(td));
	NUTS_PASS(nuts_stream_wait(tl));
	NUTS_TRUE(memcmp(bufs, bufr, size) == 0);

	nng_free(bufr, size);
	nng_free(bufs, size);
	nng_stream_free(sd);
	nng_stream_free(sl);
	nng_stream_dialer_free(d);
	nng_stream_listener_free(l);
	nng_aio_free(aiod);
	nng_aio_free(aiol);
}

TEST_LIST = {
	{ "quic dialer listener echo test", test_quic_echo },
	{ NULL, NULL },
};
