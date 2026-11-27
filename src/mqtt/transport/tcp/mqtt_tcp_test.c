//
// Copyright 2026 NanoMQ Team, Inc.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdio.h>
#include <string.h>

#include <nng/mqtt/mqtt_client.h>
#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>

#include <nuts.h>

#define MQTT_V311_NOT_AUTHORIZED 0x05

typedef struct {
	nng_stream_listener *listener;
	nng_socket           socket;
	nng_dialer           dialer;
	nng_aio             *accept_aio;
	nng_aio             *send_aio;
	nng_stream          *stream;
	uint8_t              connack[4];
} connack_server;

static void
connack_server_accept_cb(void *arg)
{
	connack_server *server = arg;
	nng_iov         iov;

	if (nng_aio_result(server->accept_aio) != 0) {
		return;
	}
	server->stream = nng_aio_get_output(server->accept_aio, 0);
	iov.iov_buf    = server->connack;
	iov.iov_len    = sizeof(server->connack);
	if (nng_aio_set_iov(server->send_aio, 1, &iov) != 0) {
		return;
	}
	nng_stream_send(server->stream, server->send_aio);
}

static void
connack_server_open(connack_server *server)
{
	nng_msg *connmsg;
	char     url[64];
	int      port;

	NUTS_PASS(nng_aio_alloc(
	    &server->accept_aio, connack_server_accept_cb, server));
	NUTS_PASS(nng_aio_alloc(&server->send_aio, NULL, NULL));
	NUTS_PASS(nng_stream_listener_alloc(
	    &server->listener, "tcp://127.0.0.1"));
	NUTS_PASS(nng_stream_listener_listen(server->listener));
	NUTS_PASS(nng_stream_listener_get_int(
	    server->listener, NNG_OPT_TCP_BOUND_PORT, &port));
	snprintf(url, sizeof(url), "mqtt-tcp://127.0.0.1:%d", port);

	NUTS_PASS(nng_mqtt_client_open(&server->socket));
	NUTS_PASS(nng_dialer_create(&server->dialer, server->socket, url));
	NUTS_PASS(nng_mqtt_msg_alloc(&connmsg, 0));
	nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
	nng_mqtt_msg_set_connect_proto_version(
	    connmsg, MQTT_PROTOCOL_VERSION_v311);
	nng_mqtt_msg_set_connect_clean_session(connmsg, true);
	NUTS_PASS(nng_dialer_set_ptr(
	    server->dialer, NNG_OPT_MQTT_CONNMSG, connmsg));

	nng_stream_listener_accept(server->listener, server->accept_aio);
}

static void
connack_server_close(connack_server *server)
{
	nng_aio_wait(server->accept_aio);
	NUTS_PASS(nng_aio_result(server->accept_aio));
	nng_aio_wait(server->send_aio);
	NUTS_PASS(nng_aio_result(server->send_aio));
	NUTS_PASS(nng_dialer_close(server->dialer));
	NUTS_PASS(nng_close(server->socket));
	nng_stream_listener_close(server->listener);
	if (server->stream != NULL) {
		nng_stream_close(server->stream);
		nng_stream_free(server->stream);
	}
	nng_stream_listener_free(server->listener);
	nng_aio_free(server->send_aio);
	nng_aio_free(server->accept_aio);
}

void
test_rejected_connack_fails_negotiation(void)
{
	connack_server server = {
		.connack = { 0x20, 0x02, 0x00, MQTT_V311_NOT_AUTHORIZED },
	};
	int reason;

	connack_server_open(&server);
	NUTS_FAIL(nng_dialer_start(server.dialer, 0), NNG_ECONNREFUSED);
	NUTS_PASS(nng_dialer_get_int(
	    server.dialer, NNG_OPT_MQTT_CONNECT_REASON, &reason));
	NUTS_TRUE(reason == MQTT_V311_NOT_AUTHORIZED);
	connack_server_close(&server);
}

static nng_msg *
qos0_publish(uint8_t payload)
{
	nng_msg *msg;
	NUTS_PASS(nng_mqtt_msg_alloc(&msg, 0));
	nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
	NUTS_PASS(nng_mqtt_msg_set_publish_topic(msg, "out/test"));
	NUTS_PASS(nng_mqtt_msg_set_publish_payload(msg, &payload, 1));
	return msg;
}

static void
test_qos0_send_rejects_invalid_arguments(void)
{
	nng_socket sock;
	nng_msg   *msg = qos0_publish(1);

#ifdef NNG_HAVE_PAIR1
	NUTS_OPEN(sock);
	NUTS_FAIL(nng_mqtt_client_send_qos0(sock, msg), NNG_ENOTSUP);
	NUTS_CLOSE(sock);
#endif
	NUTS_PASS(nng_mqtt_client_open(&sock));
	nng_mqtt_msg_set_publish_qos(msg, 1);
	NUTS_FAIL(nng_mqtt_client_send_qos0(sock, msg), NNG_EINVAL);
	nng_mqtt_msg_free_publish_buf(msg);
	nng_msg_free(msg);

	NUTS_PASS(nng_mqtt_msg_alloc(&msg, 0));
	nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PINGREQ);
	NUTS_FAIL(nng_mqtt_client_send_qos0(sock, msg), NNG_EINVAL);
	nng_msg_free(msg);
	NUTS_CLOSE(sock);
}

typedef struct {
	nng_socket      sock;
	nng_msg        *msg;
	nng_atomic_int *state;
	int             rv;
} waiting_sender;

static void
send_while_disconnected(void *arg)
{
	waiting_sender *s = arg;
	nng_atomic_set(s->state, 1);
	s->rv = nng_mqtt_client_send_qos0(s->sock, s->msg);
	nng_atomic_set(s->state, 2);
}

static void
wait_for_sender(waiting_sender *s)
{
	uint64_t deadline = nuts_clock() + 5000;
	while (nng_atomic_get(s->state) == 0 && nuts_clock() < deadline) {
		NUTS_SLEEP(1);
	}
}

static void
test_qos0_send_wakes_on_close(void)
{
	nng_socket     sock;
	waiting_sender senders[2];
	nng_thread    *threads[2];

	NUTS_PASS(nng_mqtt_client_open(&sock));
	for (unsigned i = 0; i < 2; i++) {
		senders[i].sock = sock;
		senders[i].msg  = qos0_publish((uint8_t) i);
		NUTS_PASS(nng_atomic_alloc(&senders[i].state));
		nng_atomic_set(senders[i].state, 0);
		NUTS_PASS(nng_thread_create(
		    &threads[i], send_while_disconnected, &senders[i]));
	}
	wait_for_sender(&senders[0]);
	wait_for_sender(&senders[1]);
	NUTS_SLEEP(20);
	NUTS_TRUE(nng_atomic_get(senders[0].state) == 1);
	NUTS_TRUE(nng_atomic_get(senders[1].state) == 1);
	NUTS_CLOSE(sock);

	for (unsigned i = 0; i < 2; i++) {
		waiting_sender *s = &senders[i];
		nng_thread_destroy(threads[i]);
		NUTS_FAIL(s->rv, NNG_ECLOSED);
		if (s->rv != 0) {
			uint32_t len;
			uint8_t *payload =
			    nng_mqtt_msg_get_publish_payload(s->msg, &len);
			NUTS_TRUE(len == 1 && payload != NULL && payload[0] == i);
			nng_msg_free(s->msg);
		}
		nng_atomic_free(s->state);
	}
}

static void
read_exact(nng_stream *stream, nng_aio *aio, uint8_t *buf, size_t len)
{
	while (len != 0) {
		nng_iov iov = { .iov_buf = buf, .iov_len = len };
		NUTS_PASS(nng_aio_set_iov(aio, 1, &iov));
		nng_stream_recv(stream, aio);
		nng_aio_wait(aio);
		NUTS_ASSERT(nng_aio_result(aio) == 0 && nng_aio_count(aio) > 0);
		buf += nng_aio_count(aio);
		len -= nng_aio_count(aio);
	}
}

static void
test_qos0_sends_in_order_after_connect(void)
{
	connack_server server = { .connack = { 0x20, 0x02, 0, 0 } };
	waiting_sender sender;
	nng_thread    *thread;
	nng_aio       *aio;
	uint8_t        buf[128];
	uint8_t publish[] = {
		0x30, 11, 0, 8, 'o', 'u', 't', '/', 't', 'e', 's', 't', 0,
	};

	connack_server_open(&server);
	sender.sock = server.socket;
	sender.msg  = qos0_publish(0);
	NUTS_PASS(nng_atomic_alloc(&sender.state));
	nng_atomic_set(sender.state, 0);
	NUTS_PASS(nng_thread_create(&thread, send_while_disconnected, &sender));
	wait_for_sender(&sender);
	NUTS_TRUE(nng_atomic_get(sender.state) == 1);
	NUTS_PASS(nng_dialer_start(server.dialer, 0));
	nng_thread_destroy(thread);
	NUTS_PASS(sender.rv);
	nng_atomic_free(sender.state);
	for (uint8_t i = 1; i < 3; i++) {
		NUTS_PASS(nng_mqtt_client_send_qos0(
		    server.socket, qos0_publish(i)));
	}

	nng_aio_wait(server.accept_aio);
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 2000);
	read_exact(server.stream, aio, buf, 2);
	NUTS_ASSERT(buf[0] == 0x10 && buf[1] < sizeof(buf));
	read_exact(server.stream, aio, buf, buf[1]);
	for (uint8_t i = 0; i < 3; i++) {
		publish[sizeof(publish) - 1] = i;
		read_exact(server.stream, aio, buf, sizeof(publish));
		NUTS_TRUE(memcmp(buf, publish, sizeof(publish)) == 0);
	}
	nng_aio_free(aio);
	connack_server_close(&server);
}

NUTS_TESTS = {
	{ "rejected CONNACK fails negotiation",
	    test_rejected_connack_fails_negotiation },
	{ "QoS 0 send rejects invalid arguments",
	    test_qos0_send_rejects_invalid_arguments },
	{ "QoS 0 send wakes on close", test_qos0_send_wakes_on_close },
	{ "QoS 0 sends in order after connect",
	    test_qos0_sends_in_order_after_connect },
	{ NULL, NULL },
};
