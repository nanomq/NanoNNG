//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nuts.h>

static const char *udp_test_url = "udp://127.0.0.1:8877";
static const char *udp_test_url_multicast = "udp://224.0.0.1:8877";

void
test_udp_conn_open(void)
{
	nng_stream_dialer *dialer;
	nng_aio *          aio;
	nng_stream *       s;

	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 5000); // 5 sec

	NUTS_PASS(nng_stream_dialer_alloc(&dialer, udp_test_url));
	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	NUTS_PASS(nng_aio_result(aio));

	NUTS_TRUE((s = nng_aio_get_output(aio, 0)) != NULL);

	nng_stream_free(s);
	nng_aio_free(aio);
	nng_stream_dialer_free(dialer);
}

void
test_udp_conn_send(void)
{
	nng_stream_dialer *dialer;
	nng_aio *          aio;
	nng_stream *       s;
	void *             t;
	uint8_t *          buf;
	size_t             size = 100;

	// allocate messages
	NUTS_ASSERT((buf = nng_alloc(size)) != NULL);
	for (size_t i = 0; i < size; i++) {
		buf[i] = rand() & 0xff;
	}

	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 5000); // 5 sec

	NUTS_PASS(nng_stream_dialer_alloc(&dialer, udp_test_url));
	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	NUTS_PASS(nng_aio_result(aio));

	NUTS_TRUE((s = nng_aio_get_output(aio, 0)) != NULL);
	t = nuts_stream_send_start(s, buf, size);
	NUTS_PASS(nuts_stream_wait(t));

	nng_free(buf, size);
	nng_stream_free(s);
	nng_stream_dialer_free(dialer);
	nng_aio_free(aio);
}

void
test_udp_send_recv(void)
{
	nng_stream_dialer *dialer;
	nng_aio *          aio;
	nng_stream *       s;
	void *             t;
	uint8_t *          buf, *buf2;
	size_t             size = 100;

	// allocate messages
	NUTS_ASSERT((buf = nng_alloc(size)) != NULL);
	for (size_t i = 0; i < size; i++) {
		buf[i] = rand() & 0xff;
	}
	NUTS_ASSERT((buf2 = nng_alloc(size)) != NULL);

	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 5000); // 5 sec

	NUTS_PASS(nng_stream_dialer_alloc(&dialer, udp_test_url));
	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	NUTS_PASS(nng_aio_result(aio));

	NUTS_TRUE((s = nng_aio_get_output(aio, 0)) != NULL);
	t = nuts_stream_send_start(s, buf, size);
	NUTS_PASS(nuts_stream_wait(t));

	t = nuts_stream_recv_start(s, buf2, size);
	NUTS_PASS(nuts_stream_wait(t));

	for (int i=0; i<(int)size; ++i) {
		NUTS_ASSERT(buf[i] == buf2[i]);
	}

	nng_free(buf, size);
	nng_free(buf2, size);
	nng_stream_free(s);
	nng_stream_dialer_free(dialer);
	nng_aio_free(aio);
}

void
test_udp_conn_multicast(void)
{
	nng_stream_dialer *dialer;
	nng_aio *          aio;
	nng_stream *       s;
	void *             t;
	uint8_t *          buf;
	size_t             size = 100;

	// allocate messages
	NUTS_ASSERT((buf = nng_alloc(size)) != NULL);
	for (size_t i = 0; i < size; i++) {
		buf[i] = rand() & 0xff;
	}

	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 5000); // 5 sec

	NUTS_PASS(nng_stream_dialer_alloc(&dialer, udp_test_url_multicast));
	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	NUTS_PASS(nng_aio_result(aio));

	NUTS_TRUE((s = nng_aio_get_output(aio, 0)) != NULL);
	t = nuts_stream_send_start(s, buf, size);
	NUTS_PASS(nuts_stream_wait(t));

	nng_free(buf, size);
	nng_stream_free(s);
	nng_stream_dialer_free(dialer);
	nng_aio_free(aio);
}

void
test_udp_multicast_recv(void)
{
	nng_stream_dialer *dialer;
	nng_aio *          aio;
	nng_stream *       s;
	void *             t;
	char *             buf, *buf2;
	size_t             size, size2;

	buf  = strdup("GETIPADDRESS");
	size = strlen(buf);
	size2 = 16;
	NUTS_ASSERT((buf2 = nng_alloc(size2)) != NULL);

	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 5000); // 5 sec

	NUTS_PASS(nng_stream_dialer_alloc(&dialer, udp_test_url_multicast));
	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	NUTS_PASS(nng_aio_result(aio));

	NUTS_TRUE((s = nng_aio_get_output(aio, 0)) != NULL);
	t = nuts_stream_send_start(s, buf, size);
	NUTS_PASS(nuts_stream_wait(t));

	t = nuts_stream_recv_start(s, buf2, size2);
	NUTS_PASS(nuts_stream_wait(t));
	NUTS_ASSERT(buf2 != NULL);

	printf("IP: %.*s", (int)size2, buf2);

	nng_free(buf, size);
	nng_free(buf2, size);
	nng_stream_free(s);
	nng_stream_dialer_free(dialer);
	nng_aio_free(aio);
}


TEST_LIST = {
	{ "udp conn open", test_udp_conn_open },
	{ "udp conn send", test_udp_conn_send },
	//{ "udp send then recv", test_udp_send_recv }, This need a udp listener
	//{ "udp conn multicast", test_udp_conn_multicast }, // Failed on darwin. errno 15. I don't know why...
	//{ "udp multicast and recv", test_udp_multicast_recv },
	{ NULL, NULL },
};

