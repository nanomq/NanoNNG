// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT

#include "core/nng_impl.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/protocol/mqtt/nmq_mqtt.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include <nuts.h>
#include <stdio.h>

extern int nni_nmq_test_set_pipe_id(uint32_t, uint32_t);

/* Exercise the real transport and protocol with two application workers.
 * Holding a received message models a worker still executing archive hooks.
 * No Parquet writer or application callback is mocked. */
typedef struct {
	nng_socket socket;
	conf *config;
	nng_pipe last_pipe;
	uint8_t protocol;
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
	f->last_pipe = nng_msg_get_pipe(msg);
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
connect_client_options(order_fixture *f, unsigned worker, char client_id,
    bool clean, uint16_t keepalive)
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
		0x10, 13, 0, 4, 'M', 'Q', 'T', 'T', 4, clean ? 2 : 0, keepalive >> 8, keepalive & 0xff, 0, 1, (uint8_t) client_id
	};
	start_receive(f, worker, 2000);
	if (f->protocol == MQTT_PROTOCOL_VERSION_v5) {
		uint8_t connect_v5[] = {
			0x10, 14, 0, 4, 'M', 'Q', 'T', 'T', 5, clean ? 2 : 0,
			keepalive >> 8, keepalive & 0xff, 0, 0, 1, (uint8_t) client_id
		};
		send_bytes(f, client, connect_v5, sizeof(connect_v5));
	} else {
		send_bytes(f, client, connect, sizeof(connect));
	}
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

static nng_stream *
connect_client(order_fixture *f, unsigned worker, char client_id)
{
	return connect_client_options(f, worker, client_id, true, 60);
}

static void
fixture_init_protocol(order_fixture *f, bool parquet, bool exchange,
    bool clean, uint16_t keepalive, uint8_t protocol)
{
	memset(f, 0, sizeof(*f));
	f->protocol = protocol;
	conf *config = nng_zalloc(sizeof(*config));
	NUTS_ASSERT(config != NULL);
	conf_init(config);
	f->config = config;
	config->qos_duration = 1;
	config->msq_len = 4;
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
	NUTS_ASSERT(strlen(url + 4) < sizeof(f->url));
	strcpy(f->url, url + 4);
	nng_strfree(url);
	NUTS_PASS(nng_aio_alloc(&f->io, NULL, NULL));
	nng_aio_set_timeout(f->io, 2000);
	for (unsigned w = 0; w < 2; ++w) {
		NUTS_PASS(nng_ctx_open(&f->worker[w], f->socket));
		NUTS_PASS(nng_aio_alloc(&f->receive[w], NULL, NULL));
	}
	f->client = connect_client_options(f, 0, 'a', clean, keepalive);
}

static void
fixture_init_options(order_fixture *f, bool parquet, bool exchange,
    bool clean, uint16_t keepalive)
{
	fixture_init_protocol(f, parquet, exchange, clean, keepalive, MQTT_PROTOCOL_VERSION_v311);
}

static void
fixture_init(order_fixture *f, bool parquet, bool exchange)
{
	fixture_init_options(f, parquet, exchange, true, 60);
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

static void
test_unmatched_topic(void)
{
	for (unsigned parked = 0; parked < 2; ++parked) {
		order_fixture f;
		fixture_init(&f, true, true);
		nng_strfree(f.config->exchange.nodes[0]->topic);
		f.config->exchange.nodes[0]->topic = nng_strdup("other/#");
		hold_first_publish(&f, parked != 0);
		start_receive(&f, 1, 2000);
		expect_publish(&f, 1, 'B');
		fixture_fini(&f);
	}
}

static void
test_wildcard_topic(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	nng_strfree(f.config->exchange.nodes[0]->topic);
	f.config->exchange.nodes[0]->topic = nng_strdup("+");
	hold_first_publish(&f, false);
	expect_held(&f);
	start_receive(&f, 1, 2000);
	start_receive(&f, 0, 2000);
	expect_publish(&f, 1, 'B');
	fixture_fini(&f);
}

static void
test_v5_topic_alias_order(void)
{
	const uint8_t plain[] = { 0x30, 5, 0, 1, 'x', 0, 'A' };
	const uint8_t update[] = { 0x30, 8, 0, 1, 'x', 3, 0x23, 0, 1, 'A' };
	const uint8_t reuse[] = { 0x30, 7, 0, 0, 3, 0x23, 0, 1, 'A' };
	const uint8_t *packets[] = { plain, update, reuse };
	const size_t sizes[] = { sizeof(plain), sizeof(update), sizeof(reuse) };
	for (unsigned mode = 0; mode < 3; ++mode) {
		order_fixture f;
		fixture_init_protocol(&f, true, true, true, 60, MQTT_PROTOCOL_VERSION_v5);
		start_receive(&f, 0, 2000);
		send_bytes(&f, f.client, packets[mode], sizes[mode]);
		send_bytes(&f, f.client, plain, sizeof(plain));
		free_message(take_message(&f, 0, CMD_PUBLISH));
		if (mode != 0) expect_held(&f);
		start_receive(&f, 1, 2000);
		if (mode != 0) start_receive(&f, 0, 2000);
		free_message(take_message(&f, 1, CMD_PUBLISH));
		fixture_fini(&f);
	}
}

static void
receive_bytes(order_fixture *f, const uint8_t *expected, size_t size)
{
	uint8_t bytes[16];
	NUTS_ASSERT(size <= sizeof(bytes));
	size_t got = 0;
	while (got < size) {
		nng_iov iov = { .iov_buf = bytes + got, .iov_len = size - got };
		NUTS_PASS(nng_aio_set_iov(f->io, 1, &iov));
		nng_stream_recv(f->client, f->io);
		nng_aio_wait(f->io);
		NUTS_PASS(nng_aio_result(f->io));
		size_t n = nng_aio_count(f->io);
		NUTS_ASSERT(n > 0 && n <= size - got);
		got += n;
	}
	NUTS_TRUE(memcmp(bytes, expected, size) == 0);
}

static void
ping(order_fixture *f)
{
	const uint8_t request[] = { 0xc0, 0 };
	const uint8_t response[] = { 0xd0, 0 };
	send_bytes(f, f->client, request, sizeof(request));
	receive_bytes(f, response, sizeof(response));
}

static void
test_slow_archive_keepalive(void)
{
	order_fixture f;
	fixture_init_options(&f, true, true, true, 1);
	hold_first_publish(&f, false);
	/* B is behind the held archive A, and PINGREQ is behind B on the same
	 * TCP stream. Each PINGRESP must arrive before the worker is released. */
	for (unsigned i = 0; i < 6; ++i) {
		nng_msleep(700);
		ping(&f);
	}
	expect_held(&f);
	start_receive(&f, 1, 2000);
	start_receive(&f, 0, 2000);
	expect_publish(&f, 1, 'B');
	fixture_fini(&f);
}

static void
test_deferred_fifo(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	hold_first_publish(&f, false);
	const uint8_t more[] = { 0x30, 4, 0, 1, 't', 'C', 0x30, 4, 0, 1, 't', 'D' };
	send_bytes(&f, f.client, more, sizeof(more));
	ping(&f);
	expect_held(&f);
	start_receive(&f, 1, 2000);
	NUTS_PASS(nng_ctx_close(f.worker[0]));
	f.closed[0] = true;
	expect_publish(&f, 1, 'B');
	start_receive(&f, 1, 2000);
	expect_publish(&f, 1, 'C');
	start_receive(&f, 1, 2000);
	expect_publish(&f, 1, 'D');
	fixture_fini(&f);
}

static void
test_deferred_qos_ack(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	const uint8_t first[] = { 0x30, 4, 0, 1, 't', 'A' };
	const uint8_t second[] = { 0x32, 6, 0, 1, 't', 0, 1, 'B' };
	const uint8_t puback[] = { 0x40, 2, 0, 1 };
	start_receive(&f, 0, 2000);
	send_bytes(&f, f.client, first, sizeof(first));
	expect_publish(&f, 0, 'A');
	send_bytes(&f, f.client, second, sizeof(second));
	/* A PINGRESP without an earlier PUBACK proves B is still unacknowledged. */
	ping(&f);
	expect_held(&f);
	start_receive(&f, 1, 2000);
	start_receive(&f, 0, 2000);
	free_message(take_message(&f, 1, CMD_PUBLISH));
	receive_bytes(&f, puback, sizeof(puback));
	fixture_fini(&f);
}

static void
test_deferred_disconnect(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	hold_first_publish(&f, false);
	const uint8_t disconnect[] = { 0xe0, 0 };
	start_receive(&f, 1, 2000);
	send_bytes(&f, f.client, disconnect, sizeof(disconnect));
	free_message(take_message(&f, 1, CMD_DISCONNECT_EV));
	fixture_fini(&f);
}

static void
test_deferred_overflow(void)
{
	order_fixture f;
	fixture_init(&f, true, true);
	hold_first_publish(&f, false);
	const uint8_t more[] = {
		0x32, 6, 0, 1, 't', 0, 1, 'C', 0x32, 6, 0, 1, 't', 0, 2, 'D',
		0x32, 6, 0, 1, 't', 0, 3, 'E', 0x32, 6, 0, 1, 't', 0, 4, 'F'
	};
	start_receive(&f, 1, 2000);
	send_bytes(&f, f.client, more, sizeof(more));
	free_message(take_message(&f, 1, CMD_DISCONNECT_EV));
	uint8_t byte;
	nng_iov iov = { .iov_buf = &byte, .iov_len = 1 };
	NUTS_PASS(nng_aio_set_iov(f.io, 1, &iov));
	nng_stream_recv(f.client, f.io);
	nng_aio_wait(f.io);
	/* No queued or rejected QoS publish may have been acknowledged. */
	NUTS_TRUE(nng_aio_result(f.io) != 0);
	NUTS_TRUE(nng_aio_count(f.io) == 0);
	fixture_fini(&f);
}

static void
test_zero_pipe_id(void)
{
	for (unsigned mode = 0; mode < 3; ++mode) {
		order_fixture f;
		fixture_init(&f, true, true);
		NUTS_PASS(nni_nmq_test_set_pipe_id(f.last_pipe.id, 0));
		hold_first_publish(&f, false);
		NUTS_TRUE(f.last_pipe.id == 0);
		ping(&f);
		expect_held(&f);
		start_receive(&f, 1, 2000);
		if (mode == 0) {
			start_receive(&f, 0, 2000);
		} else if (mode == 1) {
			NUTS_PASS(nng_ctx_close(f.worker[0]));
			f.closed[0] = true;
		} else {
			NUTS_PASS(nng_pipe_close(f.last_pipe));
			free_message(take_message(&f, 1, CMD_DISCONNECT_EV));
		}
		if (mode != 2) expect_publish(&f, 1, 'B');
		fixture_fini(&f);
	}
}

static void
test_cached_session_close(void)
{
	order_fixture f;
	fixture_init_options(&f, true, true, false, 60);
	hold_first_publish(&f, false);
	/* The retained session closes its old transport. Reconnecting uses a
	 * new pipe, while the old worker may still hold an archived PUBLISH. */
	NUTS_PASS(nng_pipe_close(f.last_pipe));
	nng_stream *other = connect_client_options(&f, 1, 'a', false, 60);
	const uint8_t packet[] = { 0x30, 4, 0, 1, 't', 'C' };
	start_receive(&f, 1, 2000);
	send_bytes(&f, other, packet, sizeof(packet));
	expect_publish(&f, 1, 'C');
	NUTS_PASS(nng_ctx_close(f.worker[0]));
	f.closed[0] = true;
	/* This fixture has no broker shutdown pass to expire cached sessions. */
	f.connections[1]->clean_start = 1;
	/* Session handoff suppresses disconnect events; pipe finalizers consume
	 * the application references for both old and restored connections. */
	f.connections[0] = NULL;
	f.connections[1] = NULL;
	fixture_fini(&f);
	nng_stream_free(other);
}

NUTS_TESTS = {
	{ "ordered PUBLISH resumes after worker returns", test_resume },
	{ "ordered PUBLISH resumes after owner closes", test_owner_close },
	{ "ordered PUBLISH permits socket close", test_socket_close },
	{ "ordered PUBLISH permits other connections", test_other_connection },
	{ "PUBLISH remains concurrent without archival", test_no_archive },
	{ "unmatched PUBLISH remains concurrent", test_unmatched_topic },
	{ "wildcard archive filter preserves order", test_wildcard_topic },
	{ "MQTT v5 topic aliases preserve worker order", test_v5_topic_alias_order },
	{ "control packets pass deferred publishes during slow archive", test_slow_archive_keepalive },
	{ "deferred publishes drain FIFO after owner closes", test_deferred_fifo },
	{ "deferred QoS publish ACK waits for dispatch", test_deferred_qos_ack },
	{ "DISCONNECT passes deferred publishes", test_deferred_disconnect },
	{ "deferred receive queue overflow closes without ACK", test_deferred_overflow },
	{ "zero pipe ID resumes and closes correctly", test_zero_pipe_id },
	{ "cached session closes and reconnects during archive", test_cached_session_close },
	{ NULL, NULL },
};
