//
// Copyright 2022 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>

#include "convey.h"
#include "core/nng_impl.h"

// Transport common tests.  By making a common test framework for transports,
// we can avoid rewriting the same tests for each new transport.  Include this
// file once in your test code.  The test framework uses the REQ/REP protocol
// for messaging.
typedef int (*trantest_proptest_t)(nng_msg *);

typedef struct trantest trantest;

struct trantest {
	const char *tmpl;
	char        addr[NNG_MAXADDRLEN + 1];
	nng_socket  reqsock;
	nng_socket  repsock;
	nni_sp_tran *  tran;
	int (*init)(struct trantest *);
	void (*fini)(struct trantest *);
	int (*dialer_init)(nng_dialer);
	int (*listener_init)(nng_listener);
	int (*proptest)(nng_msg *);
	void *private; // transport specific private data
};

unsigned trantest_port = 0;

extern int  notransport(void);
extern void trantest_checktran(const char *url);
extern void trantest_next_address(char *out, const char *prefix);
extern void trantest_prev_address(char *out, const char *prefix);
extern void trantest_init(trantest *tt, const char *addr);
extern int  trantest_dial(trantest *tt, nng_dialer *dp);
extern int  trantest_listen(trantest *tt, nng_listener *lp);
extern void trantest_scheme(trantest *tt);
extern void trantest_test(trantest *tt);
extern void trantest_test_extended(const char *addr, trantest_proptest_t f);
extern void trantest_test_all(const char *addr);

#ifndef NNG_TRANSPORT_ZEROTIER
#define nng_zt_register notransport
#endif
#ifndef NNG_TRANSPORT_WSS
#define nng_wss_register notransport
#endif

void
fatal(const char *msg, int rv)
{
	fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

int
notransport(void)
{
	ConveySkip("Transport not configured");
	return (NNG_ENOTSUP);
}

#define CHKTRAN(s, t)                      \
	if (strncmp(s, t, strlen(t)) == 0) \
	notransport()

void
trantest_checktran(const char *url)
{
#ifndef NNG_TRANSPORT_WSS
	CHKTRAN(url, "wss:");
#endif
#ifndef NNG_TRANSPORT_ZEROTIER
	CHKTRAN(url, "zt:");
#endif

	(void) url;
}

void
trantest_next_address(char *out, const char *prefix)
{
	trantest_checktran(prefix);

	if (trantest_port == 0) {
		char *pstr;

		// start at a different port each time -- 5000 - 10000 --
		// unless a specific port is given.
		trantest_port = nng_clock() % 5000 + 5000;
		if (((pstr = ConveyGetEnv("TEST_PORT")) != NULL) &&
		    (atoi(pstr) != 0)) {
			trantest_port = atoi(pstr);
		}
	}

	// we append the port, and for web sockets also a /test path
	(void) snprintf(out, NNG_MAXADDRLEN, "%s%u%s", prefix, trantest_port,
		prefix[0] == 'w' ? "/test" : "");
	trantest_port++;
}

void
mqtt_trantest_set_address(char *out, const char *prefix)
{
	trantest_checktran(prefix);

	trantest_port = 1883;

	// we append the port, and for web sockets also a /test path
	(void) snprintf(out, NNG_MAXADDRLEN, "%s%u%s", prefix, trantest_port,
		prefix[0] == 'w' ? "/test" : "");
}

void
trantest_prev_address(char *out, const char *prefix)
{
	trantest_port--;
	trantest_next_address(out, prefix);
}

void
trantest_init(trantest *tt, const char *addr)
{
	trantest_next_address(tt->addr, addr);

	So(nng_req_open(&tt->reqsock) == 0);
	So(nng_rep_open(&tt->repsock) == 0);

	nng_url *url;
	So(nng_url_parse(&url, tt->addr) == 0);
	tt->tran = nni_sp_tran_find(url);
	So(tt->tran != NULL);
	nng_url_free(url);
}

void
mqtt_trantest_init(trantest *tt, const char *addr)
{
	mqtt_trantest_set_address(tt->addr, addr);

	So(nng_req_open(&tt->reqsock) == 0);
	So(nng_rep_open(&tt->repsock) == 0);

	nng_url *url;
	So(nng_url_parse(&url, tt->addr) == 0);
	tt->tran = nni_mqtt_tran_find(url);
	So(tt->tran != NULL);
	nng_url_free(url);
}

void
trantest_fini(trantest *tt)
{
	nng_close(tt->reqsock);
	nng_close(tt->repsock);
}

int
trantest_dial(trantest *tt, nng_dialer *dp)
{
	nng_dialer d = NNG_DIALER_INITIALIZER;
	int        rv;

	rv = nng_dialer_create(&d, tt->reqsock, tt->addr);
	if (rv != 0) {
		return (rv);
	}
	if (tt->dialer_init != NULL) {
		if ((rv = tt->dialer_init(d)) != 0) {
			nng_dialer_close(d);
			return (rv);
		}
	}
	if ((rv = nng_dialer_start(d, 0)) != 0) {
		nng_dialer_close(d);
		return (rv);
	}
	*dp = d;
	return (0);
}


int
trantest_listen(trantest *tt, nng_listener *lp)
{
	int          rv;
	nng_listener l = NNG_LISTENER_INITIALIZER;

	rv = nng_listener_create(&l, tt->repsock, tt->addr);
	if (rv != 0) {
		return (rv);
	}
	if (tt->listener_init != NULL) {
		if ((rv = tt->listener_init(l)) != 0) {
			nng_listener_close(l);
			return (rv);
		}
	}
	if ((rv = nng_listener_start(l, 0)) != 0) {
		nng_listener_close(l);
		return (rv);
	}
	*lp = l;
	return (rv);
}

void
trantest_scheme(trantest *tt)
{
	Convey("Scheme is correct", {
		size_t l = strlen(tt->tran->tran_scheme);
		So(strncmp(tt->addr, tt->tran->tran_scheme, l) == 0);
		So(strncmp(tt->addr + l, "://", 3) == 0);
	})
}

void
trantest_conn_refused(trantest *tt)
{
	Convey("Connection refused works", {
		nng_dialer d = NNG_DIALER_INITIALIZER;

		So(trantest_dial(tt, &d) == NNG_ECONNREFUSED);
		So(nng_dialer_id(d) < 0);
		So(trantest_dial(tt, &d) == NNG_ECONNREFUSED);
		So(nng_dialer_id(d) < 0);
	});
}

void
trantest_duplicate_listen(trantest *tt)
{
	Convey("Duplicate listen rejected", {
		nng_listener l1 = NNG_LISTENER_INITIALIZER;
		nng_listener l2 = NNG_LISTENER_INITIALIZER;
		int          rv;
		rv = trantest_listen(tt, &l1);
		So(rv == 0);
		So(nng_listener_id(l1) > 0);
		So(trantest_listen(tt, &l2) == NNG_EADDRINUSE);
		So(nng_listener_id(l2) < 0);
		So(nng_listener_id(l1) != nng_listener_id(l2));
	});
}

void
trantest_listen_accept(trantest *tt)
{
	Convey("Listen and accept", {
		nng_listener l  = NNG_LISTENER_INITIALIZER;
		nng_dialer   d  = NNG_DIALER_INITIALIZER;
		nng_dialer   d0 = NNG_DIALER_INITIALIZER;
		So(trantest_listen(tt, &l) == 0);
		So(nng_listener_id(l) > 0);

		nng_msleep(500);
		So(trantest_dial(tt, &d) == 0);
		So(nng_dialer_id(d) > 0);
		So(nng_dialer_id(d0) < 0);
	});
}

void
trantest_send_recv(trantest *tt)
{
	Convey("Send and recv", {
		nng_listener l = NNG_LISTENER_INITIALIZER;
		nng_dialer   d = NNG_DIALER_INITIALIZER;
		nng_pipe     p = NNG_PIPE_INITIALIZER;
		nng_msg *    send;
		nng_msg *    recv;
		size_t       len;
		char *       url;

		So(trantest_listen(tt, &l) == 0);
		So(nng_listener_id(l) > 0);

		So(trantest_dial(tt, &d) == 0);
		So(nng_dialer_id(d) > 0);

		nng_msleep(200); // listener may be behind slightly

		send = NULL;
		So(nng_msg_alloc(&send, 0) == 0);
		So(send != NULL);
		So(nng_msg_append(send, "ping", 5) == 0);

		So(nng_sendmsg(tt->reqsock, send, 0) == 0);
		recv = NULL;
		So(nng_recvmsg(tt->repsock, &recv, 0) == 0);
		So(recv != NULL);
		So(nng_msg_len(recv) == 5);
		So(strcmp(nng_msg_body(recv), "ping") == 0);
		nng_msg_free(recv);

		len = strlen("acknowledge");
		So(nng_msg_alloc(&send, 0) == 0);
		So(nng_msg_append(send, "acknowledge", len) == 0);
		So(nng_sendmsg(tt->repsock, send, 0) == 0);
		So(nng_recvmsg(tt->reqsock, &recv, 0) == 0);
		So(recv != NULL);
		So(nng_msg_len(recv) == strlen("acknowledge"));
		So(strcmp(nng_msg_body(recv), "acknowledge") == 0);
		p = nng_msg_get_pipe(recv);
		So(nng_pipe_id(p) > 0);
		So(nng_pipe_get_string(p, NNG_OPT_URL, &url) == 0);
		So(strcmp(url, tt->addr) == 0);
		nng_strfree(url);
		nng_msg_free(recv);
	});
}
struct _params {
	nng_socket *sock;
	const char *topic;
	uint8_t *   data;
	uint32_t    data_len;
	uint8_t     qos;
};

struct _params params;

static void
disconnect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	nng_msg * msg = arg;
	nng_msg_free(msg);
	// int reason;
	// get connect reason
	// nng_pipe_get_int(p, NNG_OPT_MQTT_DISCONNECT_REASON, &reason);
	// property *prop;
	// nng_pipe_get_ptr(p, NNG_OPT_MQTT_DISCONNECT_PROPERTY, &prop);
	// nng_socket_get?
	// printf("%s: disconnected!\n", __FUNCTION__);
}

static void
connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	int reason;
	// get connect reason
	nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
	// get property for MQTT V5
	// property *prop;
	// nng_pipe_get_ptr(p, NNG_OPT_MQTT_CONNECT_PROPERTY, &prop);
	// printf("%s: connected!\n", __FUNCTION__);
}

// Connect to the given address.
int
client_connect(nng_socket *sock, const char *url, uint8_t proto_version)
{
	nng_dialer dialer;
	int        rv;

	if (proto_version == MQTT_PROTOCOL_VERSION_v311) {
		if ((rv = nng_mqtt_client_open(sock)) != 0) {
			fatal("nng_socket", rv);
		}
	}
	else if(proto_version == MQTT_PROTOCOL_VERSION_v5){
		if ((rv = nng_mqttv5_client_open(sock)) != 0) {
			fatal("nng_socket", rv);
		}
	}

	if ((rv = nng_dialer_create(&dialer, *sock, url)) != 0) {
		fatal("nng_dialer_create", rv);
	}

	// create a CONNECT message
	/* CONNECT */
	nng_msg *connmsg;
	nng_mqtt_msg_alloc(&connmsg, 0);
	nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
	nng_mqtt_msg_set_connect_proto_version(connmsg, proto_version);
	nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
	nng_mqtt_msg_set_connect_user_name(connmsg, "nng_mqtt_client");
	nng_mqtt_msg_set_connect_password(connmsg, "secrets");
	nng_mqtt_msg_set_connect_will_msg(
	    connmsg, (uint8_t *) "bye-bye", strlen("bye-bye"));
	nng_mqtt_msg_set_connect_will_topic(connmsg, "will_topic");
	nng_mqtt_msg_set_connect_clean_session(connmsg, true);

	nng_mqtt_set_connect_cb(*sock, connect_cb, &sock);
	nng_mqtt_set_disconnect_cb(*sock, disconnect_cb, connmsg);

	nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, connmsg);
	nng_dialer_start(dialer, NNG_FLAG_NONBLOCK);

	return (0);
}

void
transtest_mqtt_sub_send(nng_socket sock)
{
	nng_mqtt_topic_qos subscriptions[] = {
		{ .qos     = params.qos,
		    .topic = { .buf = (uint8_t *) params.topic,
		        .length     = strlen(params.topic) } },
	};

	nng_msg *submsg;
	nng_mqtt_msg_alloc(&submsg, 0);
	nng_mqtt_msg_set_packet_type(submsg, NNG_MQTT_SUBSCRIBE);
	nng_mqtt_msg_set_subscribe_topics(submsg, (nng_mqtt_topic_qos *)&subscriptions, 1);

	if (nng_sendmsg(sock, submsg, 0) != 0) {
		nng_msg_free(submsg);
	}
}

void
trantest_mqtt_pub(nng_socket sock)
{
	nng_msg *pubmsg;
	nng_mqtt_msg_alloc(&pubmsg, 0);
	nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(pubmsg, 0);
	nng_mqtt_msg_set_publish_qos(pubmsg, params.qos);
	nng_mqtt_msg_set_publish_retain(pubmsg, 0);
	nng_mqtt_msg_set_publish_payload(
	    pubmsg, (uint8_t *) params.data, params.data_len);
	nng_mqtt_msg_set_publish_topic(pubmsg, params.topic);
	nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK);
}

void
transtest_mqtt_sub_recv(nng_socket sock)
{
	while (1) {
		nng_msg *msg = NULL;
		uint8_t *payload;
		uint32_t payload_len;
		if (nng_recvmsg(sock, &msg, 0) != 0) {
			continue;
		}

		// we should only receive publish messages
		if (nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_PUBLISH) {
			payload = nng_mqtt_msg_get_publish_payload(
			    msg, &payload_len);
			// printf("what I get:%s\n", (char *) payload);
			So(strcmp((char *) payload, "ping") == 0);
			break;
		}

		nng_msg_free(msg);
	}
}

void
trantest_mqtt_sub_pub(trantest *tt)
{
	Convey("mqtt pub and sub", {
		const char *url   = tt->addr;
		uint8_t     qos   = 0;
		const char *topic = "myTopic";
		const char *data  = "ping";

		client_connect(&tt->reqsock, url, MQTT_PROTOCOL_VERSION_v311);
		client_connect(&tt->repsock, url, MQTT_PROTOCOL_VERSION_v311);

		params.topic    = topic;
		params.data     = (uint8_t *) data;
		params.data_len = strlen(data);
		params.qos      = qos;

		transtest_mqtt_sub_send(tt->reqsock);
		trantest_mqtt_pub(tt->repsock);
		transtest_mqtt_sub_recv(tt->reqsock);
	});
}

void
trantest_mqttv5_sub_pub(trantest *tt)
{
	Convey("mqttv5 pub and sub", {
		const char *url   = tt->addr;
		uint8_t     qos   = 0;
		const char *topic = "myTopic";
		const char *data  = "ping";

		client_connect(&tt->reqsock, url, MQTT_PROTOCOL_VERSION_v5);
		client_connect(&tt->repsock, url, MQTT_PROTOCOL_VERSION_v5);

		params.topic    = topic;
		params.data     = (uint8_t *) data;
		params.data_len = strlen(data);
		params.qos      = qos;

		transtest_mqtt_sub_send(tt->reqsock);
		trantest_mqtt_pub(tt->repsock);
		transtest_mqtt_sub_recv(tt->reqsock);
	});
}

void
trantest_send_recv_multi(trantest *tt)
{
	Convey("Send and recv multi", {
		nng_listener l = NNG_LISTENER_INITIALIZER;
		nng_dialer   d = NNG_DIALER_INITIALIZER;
		nng_pipe     p = NNG_PIPE_INITIALIZER;
		nng_msg *    send;
		nng_msg *    recv;
		char *       url;
		int          i;
		char         msgbuf[16];

		So(trantest_listen(tt, &l) == 0);
		So(nng_listener_id(l) > 0);
		So(trantest_dial(tt, &d) == 0);
		So(nng_dialer_id(d) > 0);

		nng_msleep(200); // listener may be behind slightly

		for (i = 0; i < 10; i++) {
			snprintf(msgbuf, sizeof(msgbuf), "ping%d", i);
			send = NULL;
			So(nng_msg_alloc(&send, 0) == 0);
			So(send != NULL);
			So(nng_msg_append(send, msgbuf, strlen(msgbuf) + 1) ==
			    0);

			So(nng_sendmsg(tt->reqsock, send, 0) == 0);
			recv = NULL;
			So(nng_recvmsg(tt->repsock, &recv, 0) == 0);
			So(recv != NULL);
			So(nng_msg_len(recv) == strlen(msgbuf) + 1);
			So(strcmp(nng_msg_body(recv), msgbuf) == 0);
			nng_msg_free(recv);

			snprintf(msgbuf, sizeof(msgbuf), "pong%d", i);
			So(nng_msg_alloc(&send, 0) == 0);
			So(nng_msg_append(send, msgbuf, strlen(msgbuf) + 1) ==
			    0);
			So(nng_sendmsg(tt->repsock, send, 0) == 0);
			So(nng_recvmsg(tt->reqsock, &recv, 0) == 0);
			So(recv != NULL);
			So(nng_msg_len(recv) == strlen(msgbuf) + 1);
			So(strcmp(nng_msg_body(recv), msgbuf) == 0);
			p = nng_msg_get_pipe(recv);
			So(nng_pipe_id(p) > 0);
			So(nng_pipe_get_string(p, NNG_OPT_URL, &url) == 0);
			So(strcmp(url, tt->addr) == 0);
			nng_strfree(url);
			nng_msg_free(recv);
		}
	});
}

void
trantest_check_properties(trantest *tt, trantest_proptest_t f)
{
	Convey("Properties test", {
		nng_listener l = NNG_LISTENER_INITIALIZER;
		nng_dialer   d = NNG_DIALER_INITIALIZER;
		nng_msg *    send;
		nng_msg *    recv;
		int          rv;

		So(trantest_listen(tt, &l) == 0);
		So(nng_listener_id(l) > 0);
		So(trantest_dial(tt, &d) == 0);
		So(nng_dialer_id(d) > 0);

		nng_msleep(200); // listener may be behind slightly

		send = NULL;
		So(nng_msg_alloc(&send, 0) == 0);
		So(send != NULL);
		So(nng_msg_append(send, "props", 5) == 0);

		So(nng_sendmsg(tt->reqsock, send, 0) == 0);

		recv = NULL;
		So(nng_recvmsg(tt->repsock, &recv, 0) == 0);
		So(recv != NULL);
		So(nng_msg_len(recv) == 5);
		So(strcmp(nng_msg_body(recv), "props") == 0);
		rv = f(recv);
		nng_msg_free(recv);
		So(rv == 0);
	});
}

void
trantest_send_recv_large(trantest *tt)
{
	Convey("Send and recv large data", {
		nng_listener l = NNG_LISTENER_INITIALIZER;
		nng_dialer   d = NNG_DIALER_INITIALIZER;
		nng_msg *    send;
		nng_msg *    recv;
		char *       data;
		size_t       size;

		size = 1024 * 128; // bigger than any transport segment
		So((data = nng_alloc(size)) != NULL);

		for (int i = 0; (size_t) i < size; i++) {
			data[i] = nng_random() & 0xff;
		}

		So(trantest_listen(tt, &l) == 0);
		So(nng_listener_id(l) > 0);
		So(trantest_dial(tt, &d) == 0);
		So(nng_dialer_id(d) > 0);

		nng_msleep(200); // listener may be behind slightly

		send = NULL;
		So(nng_msg_alloc(&send, size) == 0);
		So(send != NULL);
		memcpy(nng_msg_body(send), data, size);

		So(nng_sendmsg(tt->reqsock, send, 0) == 0);
		recv = NULL;
		So(nng_recvmsg(tt->repsock, &recv, 0) == 0);
		So(recv != NULL);
		So(nng_msg_len(recv) == size);
		So(memcmp(nng_msg_body(recv), data, size) == 0);
		nng_msg_free(recv);

		memset(data, 0x2, size);

		So(nng_msg_alloc(&send, 0) == 0);
		So(nng_msg_append(send, data, size) == 0);
		So(nng_sendmsg(tt->repsock, send, 0) == 0);
		So(nng_recvmsg(tt->reqsock, &recv, 0) == 0);
		So(recv != NULL);
		So(nng_msg_len(recv) == size);
		So(memcmp(nng_msg_body(recv), data, size) == 0);
		nng_msg_free(recv);

		nng_free(data, size);
	})
}

void
trantest_test_all(const char *addr)
{
	trantest tt;

	memset(&tt, 0, sizeof(tt));
	Convey("Given transport", {
		trantest_init(&tt, addr);

		Reset({ trantest_fini(&tt); });

		trantest_scheme(&tt);
		trantest_conn_refused(&tt);
		trantest_duplicate_listen(&tt);
		trantest_listen_accept(&tt);
		trantest_send_recv(&tt);
		trantest_send_recv_large(&tt);
		trantest_send_recv_multi(&tt);
	})
}

void
trantest_test_extended(const char *addr, trantest_proptest_t f)
{
	trantest tt;

	memset(&tt, 0, sizeof(tt));
	Convey("Given transport", {
		trantest_init(&tt, addr);

		Reset({ trantest_fini(&tt); });

		trantest_scheme(&tt);
		trantest_conn_refused(&tt);
		trantest_duplicate_listen(&tt);
		trantest_listen_accept(&tt);
		trantest_send_recv(&tt);
		trantest_send_recv_large(&tt);
		trantest_send_recv_multi(&tt);
		trantest_check_properties(&tt, f);
	})
}

void
mqtt_trantest_test(const char *addr)
{
	trantest tt;

	memset(&tt, 0, sizeof(tt));
	Convey("MQTT given transport", {
		mqtt_trantest_init(&tt, addr);

		Reset({ trantest_fini(&tt); });

		trantest_scheme(&tt);
		trantest_mqtt_sub_pub(&tt);
		trantest_mqttv5_sub_pub(&tt);
		// trantest_send_recv_large(&tt);
		// trantest_send_recv_multi(&tt);
		// trantest_check_properties(&tt, f);
	})
}

void
trantest_test(trantest *tt)
{
	Convey("Given transport", {
		trantest_init(tt, tt->tmpl);
		if (tt->init != NULL) {
			So(tt->init(tt) == 0);
		}

		Reset({
			if (tt->fini != NULL) {
				tt->fini(tt);
			}
			trantest_fini(tt);
		});

		trantest_scheme(tt);

		trantest_conn_refused(tt);
		trantest_duplicate_listen(tt);
		trantest_listen_accept(tt);

		trantest_send_recv(tt);
		trantest_send_recv_large(tt);
		trantest_send_recv_multi(tt);
		if (tt->proptest != NULL) {
			trantest_check_properties(tt, tt->proptest);
		}
	})
}
