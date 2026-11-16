// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT
#include "nng/mqtt/mqtt_client.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/protocol/mqtt/nmq_mqtt.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include <nuts.h>
#include <string.h>

typedef struct {
	nng_socket socket;
	nng_ctx worker[2];
	nng_aio *receive[2], *io;
	nng_stream *client;
	conn_param *connection;
	bool socket_closed;
} receive_fixture;

static void
send_bytes(receive_fixture *f, const void *data, size_t size)
{
	const uint8_t *p = data;
	while (size != 0) {
		nng_iov iov = { .iov_buf = (void *) p, .iov_len = size };
		NUTS_PASS(nng_aio_set_iov(f->io, 1, &iov));
		nng_stream_send(f->client, f->io);
		nng_aio_wait(f->io);
		NUTS_PASS(nng_aio_result(f->io));
		size_t n = nng_aio_count(f->io);
		NUTS_ASSERT(n > 0 && n <= size);
		p += n;
		size -= n;
	}
}

static void
start_receive(receive_fixture *f, unsigned worker)
{
	nng_aio_set_timeout(f->receive[worker], 2000);
	nng_ctx_recv(f->worker[worker], f->receive[worker]);
}

static nng_msg *
take_message(receive_fixture *f, unsigned worker, uint8_t type)
{
	nng_aio *aio = f->receive[worker];
	nng_aio_wait(aio);
	NUTS_PASS(nng_aio_result(aio));
	nng_msg *msg = nng_aio_get_msg(aio);
	NUTS_ASSERT(msg != NULL);
	nng_aio_set_msg(aio, NULL);
	NUTS_TRUE(nng_msg_cmd_type(msg) == type);
	return msg;
}

static void
free_message(nng_msg *msg)
{
	conn_param_free(nng_msg_get_conn_param(msg));
	nng_msg_free(msg);
}

static void
check_publish(nng_msg *msg, char payload)
{
	const uint8_t expected[] = { 0, 1, 't', (uint8_t) payload };
	NUTS_ASSERT(nng_msg_len(msg) == sizeof(expected));
	NUTS_TRUE(memcmp(nng_msg_body(msg), expected, sizeof(expected)) == 0);
}

static void
fixture_init(receive_fixture *f, bool parquet, bool exchange)
{
	memset(f, 0, sizeof(*f));
	conf *config = nng_zalloc(sizeof(*config));
	NUTS_ASSERT(config != NULL);
	conf_init(config);
	config->parquet.enable = parquet;
	if (exchange) {
		conf_exchange_node *node = nng_zalloc(sizeof(*node));
		NUTS_ASSERT(node != NULL);
		node->topic = nng_strdup("t");
		NUTS_ASSERT(node->topic != NULL);
		NUTS_PASS(nng_mtx_alloc(&node->mtx));
		cvector_push_back(config->exchange.nodes, node);
		config->exchange.count = 1;
	}
	f->socket.data = config;
	NUTS_PASS(nng_nmq_tcp0_open(&f->socket));
	nng_listener listener;
	NUTS_PASS(nng_listener_create(&listener, f->socket, "nmq-tcp://127.0.0.1:0"));
	NUTS_PASS(nng_listener_set(listener, NANO_CONF, config, sizeof(*config)));
	NUTS_PASS(nng_listener_start(listener, 0));
	char *url;
	NUTS_PASS(nng_listener_get_string(listener, NNG_OPT_URL, &url));
	NUTS_ASSERT(strncmp(url, "nmq-", 4) == 0);
	nng_stream_dialer *dialer;
	NUTS_PASS(nng_stream_dialer_alloc(&dialer, url + 4));
	nng_strfree(url);
	NUTS_PASS(nng_aio_alloc(&f->io, NULL, NULL));
	nng_aio_set_timeout(f->io, 2000);
	for (unsigned w = 0; w < 2; ++w) {
		NUTS_PASS(nng_ctx_open(&f->worker[w], f->socket));
		NUTS_PASS(nng_aio_alloc(&f->receive[w], NULL, NULL));
	}
	nng_stream_dialer_dial(dialer, f->io);
	nng_aio_wait(f->io);
	NUTS_PASS(nng_aio_result(f->io));
	f->client = nng_aio_get_output(f->io, 0);
	NUTS_ASSERT(f->client != NULL);
	nng_stream_dialer_free(dialer);
	const uint8_t connect[] = {
		0x10, 13, 0, 4, 'M', 'Q', 'T', 'T', 4, 2, 0, 60, 0, 1, 'a'
	};
	start_receive(f, 0);
	send_bytes(f, connect, sizeof(connect));
	nng_msg *connack = take_message(f, 0, CMD_CONNACK);
	/* The application owns the connection reference independently of each
	 * message reference; the raw client need not await a CONNACK reply. */
	f->connection = nng_msg_get_conn_param(connack);
	free_message(connack);
}

static void
fixture_fini(receive_fixture *f)
{
	if (!f->socket_closed) {
		for (unsigned w = 0; w < 2; ++w) NUTS_PASS(nng_ctx_close(f->worker[w]));
		NUTS_PASS(nng_close(f->socket));
	}
	conn_param_free(f->connection);
	nng_stream_free(f->client);
	for (unsigned w = 0; w < 2; ++w) nng_aio_free(f->receive[w]);
	nng_aio_free(f->io);
}

static void
test_concurrent_receive(void)
{
	for (unsigned mode = 0; mode < 3; ++mode) {
		for (unsigned parked = 0; parked < 2; ++parked) {
			receive_fixture f;
			fixture_init(&f, mode != 1, mode != 2);
			const uint8_t packets[] = {
				0x30, 4, 0, 1, 't', 'A', 0x30, 4, 0, 1, 't', 'B'
			};
			if (!parked) start_receive(&f, 0);
			send_bytes(&f, packets, sizeof(packets));
			if (parked) { nng_msleep(30); start_receive(&f, 0); }
			nng_msg *first = take_message(&f, 0, CMD_PUBLISH);
			check_publish(first, 'A');
			/* Worker 0 keeps its first message and never re-enters recv. */
			start_receive(&f, 1);
			nng_msg *second = take_message(&f, 1, CMD_PUBLISH);
			check_publish(second, 'B');
			free_message(second);
			free_message(first);
			fixture_fini(&f);
		}
	}
}

static void
test_close_pending_receive(void)
{
	receive_fixture f;
	fixture_init(&f, true, true);
	start_receive(&f, 1);
	NUTS_PASS(nng_close(f.socket));
	f.socket_closed = true;
	nng_aio_wait(f.receive[1]);
	int rv = nng_aio_result(f.receive[1]);
	NUTS_TRUE(rv == 0 || rv == NNG_ECLOSED);
	nng_msg *msg = nng_aio_get_msg(f.receive[1]);
	if (msg != NULL) {
		NUTS_TRUE(nng_msg_cmd_type(msg) == CMD_DISCONNECT_EV);
		nng_aio_set_msg(f.receive[1], NULL);
		free_message(msg);
	}
	fixture_fini(&f);
}

NUTS_TESTS = {
	{ "PUBLISH workers receive independently with archival enabled", test_concurrent_receive },
	{ "concurrent receive permits socket close", test_close_pending_receive },
	{ NULL, NULL },
};
