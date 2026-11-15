// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT

#include "nng/mqtt/mqtt_client.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/protocol/mqtt/nmq_mqtt.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include <nuts.h>
#include <stdio.h>

/* Exercise the real transport and protocol with two application workers.
 * Holding a received message models a worker still executing archive hooks.
 * No Parquet writer or application callback is mocked. */
typedef struct {
	nng_socket socket;
	nng_ctx worker[2];
	nng_aio *receive[2], *io;
	nng_stream *client;
	conn_param *connections[2];
	unsigned connection_count;
	char url[128];
	bool closed[2], socket_closed;
} order_fixture;

static void
send_bytes(order_fixture *f, nng_stream *client, const void *data, size_t size)
{
	const uint8_t *p = data;
	while (size != 0) {
		nng_iov iov = { .iov_buf = (void *) p, .iov_len = size };
		NUTS_PASS(nng_aio_set_iov(f->io, 1, &iov));
		nng_stream_send(client, f->io);
		nng_aio_wait(f->io);
		NUTS_PASS(nng_aio_result(f->io));
		size_t n = nng_aio_count(f->io);
		NUTS_ASSERT(n > 0 && n <= size);
		p += n;
		size -= n;
	}
}

static void
start_receive(order_fixture *f, unsigned worker, nng_duration timeout)
{
	nng_aio_set_timeout(f->receive[worker], timeout);
	nng_ctx_recv(f->worker[worker], f->receive[worker]);
}

static nng_msg *
take_message(order_fixture *f, unsigned worker, uint8_t type)
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
expect_publish(order_fixture *f, unsigned worker, char payload)
{
	nng_msg *msg = take_message(f, worker, CMD_PUBLISH);
	const uint8_t expected[] = { 0, 1, 't', (uint8_t) payload };
	NUTS_TRUE(nng_msg_len(msg) == sizeof(expected));
	NUTS_TRUE(memcmp(nng_msg_body(msg), expected, sizeof(expected)) == 0);
	free_message(msg);
}

static nng_stream *
connect_client(order_fixture *f, unsigned worker, char client_id)
{
	nng_stream_dialer *dialer;
	NUTS_PASS(nng_stream_dialer_alloc(&dialer, f->url));
	nng_stream_dialer_dial(dialer, f->io);
	nng_aio_wait(f->io);
	NUTS_PASS(nng_aio_result(f->io));
	nng_stream *client = nng_aio_get_output(f->io, 0);
	NUTS_ASSERT(client != NULL);
	nng_stream_dialer_free(dialer);
	uint8_t connect[] = {
		0x10, 13, 0, 4, 'M', 'Q', 'T', 'T', 4, 2, 0, 60, 0, 1, (uint8_t) client_id
	};
	start_receive(f, worker, 2000);
	send_bytes(f, client, connect, sizeof(connect));
	/* The protocol emits a synthetic CONNACK for the application to send.
	 * A raw stream permits pipelining PUBLISH without a client state machine. */
	nng_msg *connack = take_message(f, worker, CMD_CONNACK);
	NUTS_ASSERT(f->connection_count < 2);
	/* The broker application owns one connection reference until disconnect,
	 * separately from the per-message reference consumed by free_message. */
	f->connections[f->connection_count++] = nng_msg_get_conn_param(connack);
	free_message(connack);
	return client;
}

static void
fixture_init(order_fixture *f, bool parquet, bool exchange)
{
	memset(f, 0, sizeof(*f));
	conf *config = nng_zalloc(sizeof(*config));
	NUTS_ASSERT(config != NULL);
	conf_init(config);
	config->parquet.enable = parquet;
	if (exchange) {
		conf_exchange_node *node = nng_zalloc(sizeof(*node));
		NUTS_ASSERT(node != NULL);
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
	NUTS_ASSERT(strlen(url + 4) < sizeof(f->url));
	strcpy(f->url, url + 4);
	nng_strfree(url);
	NUTS_PASS(nng_aio_alloc(&f->io, NULL, NULL));
	nng_aio_set_timeout(f->io, 2000);
	for (unsigned w = 0; w < 2; ++w) {
		NUTS_PASS(nng_ctx_open(&f->worker[w], f->socket));
		NUTS_PASS(nng_aio_alloc(&f->receive[w], NULL, NULL));
	}
	f->client = connect_client(f, 0, 'a');
}

static void
fixture_fini(order_fixture *f)
{
	for (unsigned w = 0; w < 2; ++w) {
		if (!f->closed[w] && !f->socket_closed)
			NUTS_PASS(nng_ctx_close(f->worker[w]));
	}
	if (!f->socket_closed) NUTS_PASS(nng_close(f->socket));
	for (unsigned c = 0; c < f->connection_count; ++c)
		conn_param_free(f->connections[c]);
	nng_stream_free(f->client);
	for (unsigned w = 0; w < 2; ++w) nng_aio_free(f->receive[w]);
	nng_aio_free(f->io);
}

static void
hold_first_publish(order_fixture *f, bool parked)
{
	const uint8_t packets[] = {
		0x30, 4, 0, 1, 't', 'A', 0x30, 4, 0, 1, 't', 'B'
	};
	if (!parked) start_receive(f, 0, 2000);
	send_bytes(f, f->client, packets, sizeof(packets));
	if (parked) {
		/* Cover the recvpipes path as well as an already waiting worker. */
		nng_msleep(30);
		start_receive(f, 0, 2000);
	}
	expect_publish(f, 0, 'A');
}

static void
expect_held(order_fixture *f)
{
	start_receive(f, 1, 50);
	nng_aio_wait(f->receive[1]);
	NUTS_FAIL(nng_aio_result(f->receive[1]), NNG_ETIMEDOUT);
	NUTS_TRUE(nng_aio_get_msg(f->receive[1]) == NULL);
}

static void
test_resume(void)
{
	for (unsigned parked = 0; parked < 2; ++parked) {
		order_fixture f;
		fixture_init(&f, true, true);
		hold_first_publish(&f, parked != 0);
		expect_held(&f);
		start_receive(&f, 1, 2000);
		/* Only this handoff says that worker 0's archive hook completed. */
		start_receive(&f, 0, 2000);
		expect_publish(&f, 1, 'B');
		nng_aio_cancel(f.receive[0]);
		nng_aio_wait(f.receive[0]);
		NUTS_FAIL(nng_aio_result(f.receive[0]), NNG_ECANCELED);
		fixture_fini(&f);
	}
}

static void
test_owner_close(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	hold_first_publish(&f, false);
	expect_held(&f);
	start_receive(&f, 1, 2000);
	NUTS_PASS(nng_ctx_close(f.worker[0]));
	f.closed[0] = true;
	expect_publish(&f, 1, 'B');
	fixture_fini(&f);
}

static void
test_socket_close(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	hold_first_publish(&f, false);
	expect_held(&f);
	start_receive(&f, 1, 2000);
	NUTS_PASS(nng_close(f.socket));
	f.socket_closed = true;
	nng_aio_wait(f.receive[1]);
	int rv = nng_aio_result(f.receive[1]);
	/* Closing a pipe may deliver its disconnect event before contexts close. */
	if (rv == 0) {
		free_message(take_message(&f, 1, CMD_DISCONNECT_EV));
	} else {
		NUTS_FAIL(rv, NNG_ECLOSED);
	}
	fixture_fini(&f);
}

static void
test_other_connection(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	hold_first_publish(&f, false);
	expect_held(&f);
	nng_stream *other = connect_client(&f, 1, 'b');
	const uint8_t packet[] = { 0x30, 4, 0, 1, 't', 'C' };
	start_receive(&f, 1, 2000);
	send_bytes(&f, other, packet, sizeof(packet));
	expect_publish(&f, 1, 'C');
	fixture_fini(&f);
	nng_stream_free(other);
}

static void
test_no_archive(void)
{
	for (unsigned mode = 0; mode < 2; ++mode) {
		order_fixture f;
		fixture_init(&f, mode == 1, mode == 0);
		hold_first_publish(&f, false);
		/* Existing concurrent delivery remains when archival is disabled. */
		start_receive(&f, 1, 2000);
		expect_publish(&f, 1, 'B');
		fixture_fini(&f);
	}
}

NUTS_TESTS = {
	{ "ordered PUBLISH resumes after worker returns", test_resume },
	{ "ordered PUBLISH resumes after owner closes", test_owner_close },
	{ "ordered PUBLISH permits socket close", test_socket_close },
	{ "ordered PUBLISH permits other connections", test_other_connection },
	{ "PUBLISH remains concurrent without archival", test_no_archive },
	{ NULL, NULL },
};
