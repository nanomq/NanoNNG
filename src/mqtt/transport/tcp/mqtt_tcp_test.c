//
// Copyright 2026 NanoMQ Team, Inc.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdio.h>

#include <nng/mqtt/mqtt_client.h>
#include <nng/nng.h>

#include <nuts.h>

#define MQTT_V311_NOT_AUTHORIZED 0x05

typedef struct {
	nng_aio    *accept_aio;
	nng_aio    *send_aio;
	nng_stream *stream;
	uint8_t     connack[4];
} reject_server;

static void
reject_server_accept_cb(void *arg)
{
	reject_server *server = arg;
	nng_iov        iov;

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

void
test_rejected_connack_fails_negotiation(void)
{
	reject_server server = {
		.connack = { 0x20, 0x02, 0x00, MQTT_V311_NOT_AUTHORIZED },
	};
	nng_stream_listener *listener = NULL;
	nng_socket           socket;
	nng_dialer           dialer;
	nng_msg             *connmsg = NULL;
	char                 url[64];
	int                  port;
	int                  reason;

	NUTS_PASS(nng_aio_alloc(
	    &server.accept_aio, reject_server_accept_cb, &server));
	NUTS_PASS(nng_aio_alloc(&server.send_aio, NULL, NULL));
	NUTS_PASS(nng_stream_listener_alloc(&listener, "tcp://127.0.0.1"));
	NUTS_PASS(nng_stream_listener_listen(listener));
	NUTS_PASS(nng_stream_listener_get_int(
	    listener, NNG_OPT_TCP_BOUND_PORT, &port));
	snprintf(url, sizeof(url), "mqtt-tcp://127.0.0.1:%d", port);

	NUTS_PASS(nng_mqtt_client_open(&socket));
	NUTS_PASS(nng_dialer_create(&dialer, socket, url));
	NUTS_PASS(nng_mqtt_msg_alloc(&connmsg, 0));
	nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
	nng_mqtt_msg_set_connect_proto_version(
	    connmsg, MQTT_PROTOCOL_VERSION_v311);
	nng_mqtt_msg_set_connect_clean_session(connmsg, true);
	NUTS_PASS(nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, connmsg));

	nng_stream_listener_accept(listener, server.accept_aio);
	NUTS_FAIL(nng_dialer_start(dialer, 0), NNG_ECONNREFUSED);
	NUTS_PASS(
	    nng_dialer_get_int(dialer, NNG_OPT_MQTT_CONNECT_REASON, &reason));
	NUTS_TRUE(reason == MQTT_V311_NOT_AUTHORIZED);

	nng_aio_wait(server.accept_aio);
	NUTS_PASS(nng_aio_result(server.accept_aio));
	nng_aio_wait(server.send_aio);
	NUTS_PASS(nng_aio_result(server.send_aio));

	NUTS_PASS(nng_dialer_close(dialer));
	NUTS_PASS(nng_close(socket));
	nng_stream_listener_close(listener);
	if (server.stream != NULL) {
		nng_stream_close(server.stream);
		nng_stream_free(server.stream);
	}
	nng_stream_listener_free(listener);
	nng_aio_free(server.send_aio);
	nng_aio_free(server.accept_aio);
}

NUTS_TESTS = {
	{ "rejected CONNACK fails negotiation",
	    test_rejected_connack_fails_negotiation },
	{ NULL, NULL },
};
