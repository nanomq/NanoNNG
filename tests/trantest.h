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
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/protocol/mqtt/nmq_mqtt.h"

#include "convey.h"
#include "core/nng_impl.h"
typedef struct nano_work {
	enum {
		INIT,
		RECV,
		WAIT,
		SEND, // Actions after sending msg
		HOOK, // Rule Engine
		END,  // Clear state and cache before disconnect
		CLOSE // sending disconnect packet and err code
	} state;
	// 0x00 mqtt_broker
	// 0x01 mqtt_bridge
	uint8_t proto;
	// MQTT version cache
	uint8_t     proto_ver;
	uint8_t     flag; // flag for webhook & rule_engine
	nng_aio *   aio;
	nng_msg *   msg;
	nng_msg **  msg_ret;
	nng_ctx     ctx;        // ctx for mqtt broker
	nng_ctx     extra_ctx; //  ctx for bridging/http post
	nng_pipe    pid;
	conf *      config;
	reason_code code; // MQTT reason code

	nng_socket webhook_sock;

	struct pipe_content *     pipe_ct;
	conn_param *              cparam;
	struct pub_packet_struct *pub_packet;
	packet_subscribe *        sub_pkt;
	packet_unsubscribe *      unsub_pkt;

	void *sqlite_db;
} nano_work;

// MQTT Fixed header
struct fixed_header {
	// flag_bits
	uint8_t retain : 1;
	uint8_t qos : 2;
	uint8_t dup : 1;
	// packet_types
	uint8_t packet_type : 4;
	// remaining length
	uint32_t remain_len;
};

// MQTT Variable header
union variable_header {
	struct {
		uint16_t           packet_id;
		struct mqtt_string topic_name;
		property           *properties;
		uint32_t           prop_len;
	} publish;

	struct {
		uint16_t    packet_id;
		reason_code reason_code;
		property    *properties;
		uint32_t    prop_len;
	} pub_arrc, puback, pubrec, pubrel, pubcomp;
};

struct mqtt_payload {
	uint8_t *data;
	uint32_t len;
};

struct pub_packet_struct {
	struct fixed_header   fixed_header;
	union variable_header var_header;
	struct mqtt_payload   payload;
};

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

struct nano_work *work = NULL;

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
mqtt_broker_trantest_init(trantest *tt, const char *addr)
{
	mqtt_trantest_set_address(tt->addr, addr);

	So(nng_req_open(&tt->reqsock) == 0);

	nng_url *url;
	So(nng_url_parse(&url, tt->addr) == 0);
	tt->tran = nni_sp_tran_find(url);
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
	nng_msg *msg = arg;
	nng_msg_free(msg);

	NNI_ARG_UNUSED(p);
	NNI_ARG_UNUSED(ev);

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
	NNI_ARG_UNUSED(p);
	NNI_ARG_UNUSED(ev);
	NNI_ARG_UNUSED(arg);

	// int reason;
	// get connect reason
	// nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
	// get property for MQTT V5
	// property *prop;
	// nng_pipe_get_ptr(p, NNG_OPT_MQTT_CONNECT_PROPERTY, &prop);
	// printf("%s: connected!\n", __FUNCTION__);
}
static void
send_callback(nng_mqtt_client *client, nng_msg *msg, void *arg)
{
	NNI_ARG_UNUSED(client);
	NNI_ARG_UNUSED(msg);
	NNI_ARG_UNUSED(arg);

	// nng_aio *        aio    = client->send_aio;
	// uint32_t         count;
	// uint8_t *        code;
	// uint8_t          type;

	if (msg == NULL)
		return;
	switch (nng_mqtt_msg_get_packet_type(msg)) {
	case NNG_MQTT_CONNACK:
		printf("connack!\n");
		break;
	case NNG_MQTT_SUBACK:
		// code = (reason_code *) nng_mqtt_msg_get_suback_return_codes(
		//     msg, &count);
		printf("SUBACK reason codes are\n");
		// for (int i = 0; i < count; ++i)
		// 	printf("%d ", code[i]);
		// printf("\n");
		break;
	case NNG_MQTT_UNSUBACK:
		// code = (reason_code *)
		// nng_mqtt_msg_get_unsuback_return_codes(
		//     msg, &count);
		printf("UNSUBACK reason codes are\n");
		// for (int i = 0; i < count; ++i)
		// 	printf("%d ", code[i]);
		// printf("\n");
		break;
	case NNG_MQTT_PUBACK:
		printf("PUBACK\n");
		break;
	default:
		// printf("Sending in async way is done.\n");
		// printf("default\n");
		break;
	}
	// printf("aio mqtt result %d \n", nng_aio_result(aio));
	// printf("suback %d \n", *code);
	nng_msg_free(msg);
}

// Connect to the given address.
int
client_connect(nng_socket *sock, nng_dialer *dialer, const char *url, uint8_t proto_version)
{
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

	if ((rv = nng_dialer_create(dialer, *sock, url)) != 0) {
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

	nng_dialer_set_ptr(*dialer, NNG_OPT_MQTT_CONNMSG, connmsg);
	nng_dialer_start(*dialer, NNG_FLAG_NONBLOCK);

	return (0);
}

void
transtest_mqtt_sub_send(nng_socket sock, nng_mqtt_client *client, bool async)
{
	nng_mqtt_topic_qos subscriptions[] = {
		{ .qos     = params.qos,
		    .topic = { .buf = (uint8_t *) params.topic,
		        .length     = strlen(params.topic) } },
	};
	size_t topic_cnt = 1;

	if (async) {
		So(nng_mqtt_subscribe_async(client, subscriptions, topic_cnt, NULL) == 0);
	} else {
		So(nng_mqtt_subscribe(sock, subscriptions, topic_cnt, NULL) == 0);
	}
}

void
transtest_mqtt_unsub_send(nng_socket sock, nng_mqtt_client *client, bool async)
{
	nng_mqtt_topic unsubscriptions[] = {
		{
		    .buf    = (uint8_t *) params.topic,
		    .length = strlen(params.topic),
		},
	};
	size_t topic_cnt = 1;

	if (async) {
		So(nng_mqtt_unsubscribe_async(client, unsubscriptions, topic_cnt, NULL) == 0);
	} else {
		So(nng_mqtt_unsubscribe(sock, unsubscriptions, topic_cnt, NULL) == 0);
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
	So(nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK) == 0);

	conn_param *cp  = NULL;
	nng_msg    *msg = NULL;
	nng_recvmsg(sock, &msg, 0);
	if (nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_CONNACK) {
		cp = nng_msg_get_conn_param(msg);
		nng_msg_free(msg);
	}
	conn_param_free(cp);
}

void
transtest_mqtt_sub_recv(nng_socket sock)
{
	conn_param *cp = NULL;

	nng_msg *msg = NULL;
	uint8_t *payload;
	uint32_t payload_len;
	
	while (1) {
		// recv msg synchronously
		if (nng_recvmsg(sock, &msg, 0) != 0) {
			continue;
		}
		// we should only receive publish messages
		nng_mqtt_packet_type type = nng_mqtt_msg_get_packet_type(msg);
		if (type == NNG_MQTT_PUBLISH) {
			payload = nng_mqtt_msg_get_publish_payload(
			    msg, &payload_len);
			// printf("what I get:%s\n", (char *) payload);
			So(strcmp((char *) payload, "ping") == 0);
			nng_msg_free(msg);
			break;
		} else if (type == NNG_MQTT_CONNACK) {
			cp = nng_msg_get_conn_param(msg);
			nng_msg_free(msg);
		}
	}
	conn_param_free(cp);
}

void
trantest_mqtt_sub_pub(trantest *tt)
{
	Convey("mqtt pub and sub", {
		const char *url   = tt->addr;
		uint8_t     qos   = 2;
		const char *topic = "myTopic";
		const char *data  = "ping";
		nng_dialer  subdialer;
		nng_dialer  pubdialer;
		nng_mqtt_client *client = NULL;

		client_connect(&tt->reqsock, &subdialer, url, MQTT_PROTOCOL_VERSION_v311);
		client_connect(&tt->repsock, &pubdialer, url, MQTT_PROTOCOL_VERSION_v311);

		params.topic    = topic;
		params.data     = (uint8_t *) data;
		params.data_len = strlen(data);
		params.qos      = qos;

		So((client = nng_mqtt_client_alloc(tt->reqsock, &send_callback, true)) != NULL);
		transtest_mqtt_sub_send(tt->reqsock, client, true);
		nng_msleep(200);// make sure the server recv sub msg before we send pub msg.
		trantest_mqtt_pub(tt->repsock);
		transtest_mqtt_sub_recv(tt->reqsock);
		transtest_mqtt_unsub_send(tt->reqsock, client, true);
		nng_mqtt_client_free(client, true);

	});
}


void
trantest_mqttv5_sub_pub(trantest *tt)
{
	Convey("mqttv5 pub and sub", {
		const char *url   = tt->addr;
		uint8_t     qos   = 2;
		const char *topic = "myTopic";
		const char *data  = "ping";
		nng_dialer  subdialer;
		nng_dialer  pubdialer;
		nng_mqtt_client *client = NULL;

		client_connect(&tt->reqsock, &subdialer, url, MQTT_PROTOCOL_VERSION_v5);
		client_connect(&tt->repsock, &pubdialer, url, MQTT_PROTOCOL_VERSION_v5);

		params.topic    = topic;
		params.data     = (uint8_t *) data;
		params.data_len = strlen(data);
		params.qos      = qos;

		So((client = nng_mqtt_client_alloc(tt->reqsock, &send_callback, true)) != NULL);
		transtest_mqtt_sub_send(tt->reqsock, client, true);
		nng_msleep(200); // make sure the server recv sub msg before we send pub msg.
		trantest_mqtt_pub(tt->repsock);
		transtest_mqtt_sub_recv(tt->reqsock);
		transtest_mqtt_unsub_send(tt->reqsock, client, true);
		nng_mqtt_client_free(client, true);
	});
}

void
server_cb(void *arg)
{
	struct nano_work *work = arg;
	NNI_ARG_UNUSED(work); // to be used.
	// printf("AIO START\n");
}

void
trantest_broker_start(trantest *tt, nng_listener listener)
{
	// alloc and init nmq_conf
	conf *nanomq_conf = NULL;
	So((nanomq_conf = nng_zalloc(sizeof(conf))) != NULL);
	conf_init(nanomq_conf);

	// set sock.date and open sock.
	tt->repsock.data = nanomq_conf;
	So(nng_nmq_tcp0_open(&tt->repsock) == 0);

	So(nng_listener_create(&listener, tt->repsock, tt->addr) == 0);
	So(nng_listener_set(listener, NANO_CONF, nanomq_conf, sizeof(conf)) == 0);
	So(nng_listener_start(listener, 0) == 0);

	// alloc and init work
	So((work = nng_alloc(sizeof(*work))) != NULL);
	So((nng_aio_alloc(&work->aio, server_cb, work)) == 0);
	So((nng_ctx_open(&work->ctx, tt->repsock)) == 0);
	work->state  = INIT;
	work->proto  = 0;
	work->config = nanomq_conf;
	work->code   = SUCCESS;
}

int
decode_sub_msg(nano_work *work)
{
	uint8_t *variable_ptr, *payload_ptr;
	int      vpos          = 0; // pos in variable
	int      bpos          = 0; // pos in payload
	size_t   len_of_varint = 0, len_of_property = 0, len_of_properties = 0;
	int      len_of_str = 0, len_of_topic = 0;
	uint8_t  property_id;

	topic_node *       tn, *_tn;

	nng_msg *     msg           = work->msg;
	size_t        remaining_len = nng_msg_remaining_len(msg);
	const uint8_t proto_ver     = work->proto_ver;

	// handle variable header
	variable_ptr = nng_msg_body(msg);

	packet_subscribe *sub_pkt = work->sub_pkt;
	sub_pkt->node = NULL;
	NNI_GET16(variable_ptr + vpos, sub_pkt->packet_id);
	if (sub_pkt->packet_id == 0)
		return PROTOCOL_ERROR; // packetid should be non-zero
	// TODO packetid should be checked if it's unused
	vpos += 2;

	sub_pkt->properties = NULL;
	sub_pkt->prop_len   = 0;
	// Only Mqtt_v5 include property.
	if (MQTT_PROTOCOL_VERSION_v5 == proto_ver) {
		sub_pkt->properties =
		    decode_properties(msg, (uint32_t *)&vpos, &sub_pkt->prop_len, true);
		if (check_properties(sub_pkt->properties) != SUCCESS) {
			return PROTOCOL_ERROR;
		}
	}

	log_debug("remainLen: [%ld] packetid : [%d]", remaining_len,
	    sub_pkt->packet_id);
	// handle payload
	payload_ptr = nng_msg_payload_ptr(msg);

	if ((tn = nng_zalloc(sizeof(topic_node))) == NULL) {
		log_error("nng_zalloc");
		return NNG_ENOMEM;
	}
	tn->next = NULL;
	sub_pkt->node      = tn;

	while (1) {
		_tn      = tn;

		tn->reason_code  = GRANTED_QOS_2; // default

		// TODO Decoding topic has potential buffer overflow
		tn->topic.body =
		    (char *)copy_utf8_str(payload_ptr, (uint32_t *)&bpos, &len_of_topic);
		tn->topic.len = len_of_topic;
		log_info("topic: [%s] len: [%d]", tn->topic.body, len_of_topic);
		len_of_topic = 0;

		if (tn->topic.len < 1 || tn->topic.body == NULL) {
			log_error("NOT utf8-encoded string OR null string.");
			tn->reason_code = UNSPECIFIED_ERROR;
			if (MQTT_PROTOCOL_VERSION_v5 == proto_ver)
				tn->reason_code = TOPIC_FILTER_INVALID;
			bpos += 1; // ignore option
			goto next;
		}

		tn->rap = 1; // Default Setting
		memcpy(tn, payload_ptr + bpos, 1);
		if (tn->retain_handling > 2) {
			log_error("error in retain_handling");
			tn->reason_code = UNSPECIFIED_ERROR;
			return PROTOCOL_ERROR;
		}
		bpos ++;

		// Setting no_local on shared subscription is invalid
		if (MQTT_PROTOCOL_VERSION_v5 == proto_ver &&
		    strncmp(tn->topic.body, "$share/", strlen("$share/")) == 0 &&
		    tn->no_local == 1) {
			tn->reason_code = UNSPECIFIED_ERROR;
			return PROTOCOL_ERROR;
		}

next:
		if (bpos < remaining_len - vpos) {
			if (NULL == (tn = nng_zalloc(sizeof(topic_node)))) {
				log_error("nng_zalloc");
				return NNG_ENOMEM;
			}
			tn->next = NULL;
			_tn->next  = tn;
		} else {
			break;
		}
	}
	return 0;
}

int
encode_suback_msg(nng_msg *msg, nano_work *work)
{
	nng_msg_header_clear(msg);
	nng_msg_clear(msg);

	uint8_t     packet_id[2];
	uint8_t     varint[4];
	uint8_t     reason_code, cmd;
	uint32_t    remaining_len, len_of_properties;
	int         len_of_varint, rv;
	topic_node *tn;

	packet_subscribe *sub_pkt;
	if ((sub_pkt = work->sub_pkt) == NULL)
		return (-1);

	const uint8_t proto_ver = work->proto_ver;

	// handle variable header first
	NNI_PUT16(packet_id, sub_pkt->packet_id);
	if ((rv = nng_msg_append(msg, packet_id, 2)) != 0) {
		log_error("nng_msg_append [%d]", rv);
		return PROTOCOL_ERROR;
	}

	if (MQTT_PROTOCOL_VERSION_v5 == proto_ver) { // add property in variable
		encode_properties(msg, NULL, CMD_SUBACK);
	}

	// Note. packetid should be non-zero, BUT in order to make subclients
	// known that, we return an error(ALREADY IN USE)
	reason_code = PACKET_IDENTIFIER_IN_USE;
	if (sub_pkt->packet_id == 0) {
		if ((rv = nng_msg_append(msg, &reason_code, 1)) != 0) {
			log_error("nng_msg_append [%d]", rv);
			return PROTOCOL_ERROR;
		}
	}

	// Note. When packet_id is zero, topic node must be empty. So, Dont worry
	// about that the order of reason codes would be changed.
	// handle payload
	tn = sub_pkt->node;
	while (tn) {
		reason_code = tn->reason_code;
		// MQTT_v3: 0x00-qos0  0x01-qos1  0x02-qos2  0x80-fail
		if ((rv = nng_msg_append(msg, &reason_code, 1)) != 0) {
			log_error("nng_msg_append [%d]", rv);
			return PROTOCOL_ERROR;
		}
		tn = tn->next;
		log_debug("reason_code: [%x]", reason_code);
	}

	// If NOT find any reason codes
	if (!sub_pkt->node && sub_pkt->packet_id != 0) {
		reason_code = UNSPECIFIED_ERROR;
		if ((rv = nng_msg_append(msg, &reason_code, 1)) != 0) {
			log_error("nng_msg_append [%d]", rv);
			return PROTOCOL_ERROR;
		}
	}

	// handle fixed header
	cmd = CMD_SUBACK;
	if ((rv = nng_msg_header_append(msg, (uint8_t *) &cmd, 1)) != 0) {
		log_error("nng_msg_header_append [%d]", rv);
		return PROTOCOL_ERROR;
	}

	remaining_len = (uint32_t) nng_msg_len(msg);
	len_of_varint = put_var_integer(varint, remaining_len);
	if ((rv = nng_msg_header_append(msg, varint, len_of_varint)) != 0) {
		log_error("nng_msg_header_append [%d]", rv);
		return PROTOCOL_ERROR;
	}

	log_debug("remain: [%d] "
	          "varint: [%d %d %d %d] "
	          "len: [%d] "
	          "packetid: [%x %x] ",
	    remaining_len, varint[0], varint[1], varint[2], varint[3],
	    len_of_varint, packet_id[0], packet_id[1]);

	return 0;
}

int
decode_unsub_msg(nano_work *work)
{
	uint8_t *variable_ptr;
	uint8_t *payload_ptr;
	uint32_t vpos = 0; // pos in variable
	uint32_t bpos = 0; // pos in payload

	uint32_t len_of_varint = 0, len_of_property = 0, len_of_properties = 0;
	uint32_t len_of_str = 0, len_of_topic;

	packet_unsubscribe *unsub_pkt     = work->unsub_pkt;
	nng_msg *           msg           = work->msg;
	size_t              remaining_len = nng_msg_remaining_len(msg);

	uint8_t property_id;
	topic_node *       tn, *_tn;

	const uint8_t proto_ver = work->proto_ver;

	// handle varibale header
	variable_ptr = nng_msg_body(msg);
	NNI_GET16(variable_ptr, unsub_pkt->packet_id);
	vpos += 2;

	unsub_pkt->properties = NULL;
	unsub_pkt->prop_len = 0;
	// Mqtt_v5 include property
	unsub_pkt->properties = NULL;
	if (MQTT_PROTOCOL_VERSION_v5 == proto_ver) {
		unsub_pkt->properties =
		    decode_properties(msg, &vpos, &unsub_pkt->prop_len, false);
		if (check_properties(unsub_pkt->properties) != SUCCESS) {
			return PROTOCOL_ERROR;
		}
	}

	log_debug("remain_len: [%ld] packet_id : [%d]", remaining_len,
	    unsub_pkt->packet_id);

	// handle payload
	payload_ptr = nng_msg_payload_ptr(msg);

	if ((tn = nng_alloc(sizeof(topic_node))) == NULL) {
		log_debug("nng_alloc");
		return NNG_ENOMEM;
	}
	unsub_pkt->node = tn;
	tn->next = NULL;

	while (1) {
		_tn = tn;

		len_of_topic = get_utf8_str(&tn->topic.body, payload_ptr, &bpos);
		if (len_of_topic != -1) {
			tn->topic.len = len_of_topic;
		} else {
			tn->reason_code = UNSPECIFIED_ERROR;
			log_debug("not utf-8 format string.");
			return PROTOCOL_ERROR;
		}

		log_debug("bpos+vpos: [%d] remain_len: [%ld]", bpos + vpos,
		    remaining_len);
		if (bpos < remaining_len - vpos) {
			if ((tn = nng_alloc(sizeof(topic_node))) == NULL) {
				log_debug("nng_alloc");
				return NNG_ENOMEM;
			}
			tn->next  = NULL;
			_tn->next = tn;
		} else {
			break;
		}
	}
	return 0;
}

int
encode_unsuback_msg(nng_msg *msg, nano_work *work)
{
	nng_msg_header_clear(msg);
	nng_msg_clear(msg);

	uint8_t     packet_id[2];
	uint8_t     varint[4];
	uint8_t     reason_code, cmd, property_len = 0;
	uint32_t    remaining_len;
	int         len_of_varint, rv;
	topic_node *tn;

	packet_unsubscribe *unsub_pkt = work->unsub_pkt;
	const uint8_t       proto_ver = work->proto_ver;

	// handle variable header first
	NNI_PUT16(packet_id, unsub_pkt->packet_id);
	if ((rv = nng_msg_append(msg, packet_id, 2)) != 0) {
		log_debug("nng_msg_append");
		return PROTOCOL_ERROR;
	}

	if (MQTT_PROTOCOL_VERSION_v5 == proto_ver) {
		//TODO set property if necessary 
		encode_properties(msg, NULL, CMD_UNSUBACK);
	}

	// handle payload
	// no payload in mqtt_v3
	if (MQTT_PROTOCOL_VERSION_v5 == proto_ver) {
		tn = unsub_pkt->node;
		while (tn) {
			reason_code = tn->reason_code;
			if ((rv = nng_msg_append(
			         msg, (uint8_t *) &reason_code, 1)) != 0) {
				log_debug("nng_msg_append [%d]", rv);
				return PROTOCOL_ERROR;
			}
			tn = tn->next;
			log_debug("reason_code: [%x]", reason_code);
		}
	}

	// handle fixed header
	cmd = CMD_UNSUBACK;
	if ((rv = nng_msg_header_append(msg, (uint8_t *) &cmd, 1)) != 0) {
		log_debug("nng_msg_header_append [%d]", rv);
		return PROTOCOL_ERROR;
	}

	remaining_len = (uint32_t) nng_msg_len(msg);
	len_of_varint = put_var_integer(varint, remaining_len);
	if ((rv = nng_msg_header_append(msg, varint, len_of_varint)) != 0) {
		log_debug("nng_msg_header_append [%d]", rv);
		return PROTOCOL_ERROR;
	}

	log_debug("unsuback:"
	          " remain: [%d]"
	          " varint: [%d %d %d %d]"
	          " len: [%d]"
	          " packet_id: [%x %x]",
	    remaining_len, varint[0], varint[1], varint[2], varint[3],
	    len_of_varint, packet_id[0], packet_id[1]);

	return 0;
}

void
trantest_mqtt_broker_send_recv(trantest *tt)
{
	Convey("mqtt broker pub and sub", {
		const char      *url   = "mqtt-tcp://127.0.0.1:1883";
		uint8_t          qos   = 0;
		const char      *topic = "myTopic";
		const char      *data  = "ping";
		nng_dialer       dialer;
		nng_mqtt_client *client = NULL; // will be used in sub and pub.
		nng_listener     listener;
		nng_msg         *rmsg = NULL;
		nng_msg         *msg  = NULL;
		conn_param      *cp = NULL;
		conn_param      *rcp  = NULL;

		params.topic    = topic;
		params.data     = (uint8_t *) data;
		params.data_len = strlen(data);
		params.qos      = qos;

		trantest_broker_start(tt, listener);
		// create client and send CONNECT msg to establish connection.
		client_connect(&tt->reqsock, &dialer, url, MQTT_PROTOCOL_VERSION_v311);
		So((client = nng_mqtt_client_alloc(tt->reqsock, &send_callback, true)) != NULL);

		// recv aio may be slightly behind.
		nng_msleep(100);

		// server recv CONNECT msg.
		nng_ctx_recv(work->ctx, work->aio);
		So((rmsg = nng_aio_get_msg(work->aio)) != NULL);
		// we don't need conn_parm in trantest so we just free it.
		So((cp = nng_msg_get_conn_param(rmsg)) != NULL);
		// send CONNACK back to the client.
		nng_aio_set_msg(work->aio, rmsg);
		nng_ctx_send(work->ctx, work->aio);
		// nng_sendmsg(tt->repsock, rmsg, 0);
		// cp is cloned in protocol and app layer, so we free it twice.
		conn_param_free(cp);
		conn_param_free(cp);

		// client recv CONNACK msg.
		So(nng_recvmsg(tt->reqsock, &msg, 0) == 0);
		So(nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_CONNACK);
		rcp = nng_msg_get_conn_param(msg);
		// nng_msg_free(msg);

		// client send sub & server send suback.
		transtest_mqtt_sub_send(tt->reqsock, client, true);
		nng_msleep(100);
		nng_ctx_recv(work->ctx, work->aio);
		So((rmsg = nng_aio_get_msg(work->aio)) != NULL);
		So(nng_msg_get_type(rmsg) == CMD_SUBSCRIBE);
		// decode sub msg and encode suback msg.
		So((work->sub_pkt = nng_alloc(sizeof(packet_subscribe))) != NULL);
		memset(work->sub_pkt, '\0', sizeof(packet_subscribe));
		work->msg = rmsg;
		work->pid       = nng_msg_get_pipe(work->msg);
		work->cparam    = nng_msg_get_conn_param(work->msg);
		work->proto_ver = conn_param_get_protover(work->cparam);
		So(decode_sub_msg(work) == 0);
		So(encode_suback_msg(rmsg, work) == 0);
		// sub_pkt_free(work->sub_pkt);
		nng_msg_set_cmd_type(rmsg, CMD_SUBACK);
		nng_aio_set_msg(work->aio, rmsg);
		nng_ctx_send(work->ctx, work->aio);

		// client send pub & server send puback
		// trantest_mqtt_pub(tt->reqsock);
		// nng_msleep(100);
		// nng_ctx_recv(work->ctx, work->aio);
		// So((rmsg = nng_aio_get_msg(work->aio)) != NULL);
		// So(nng_msg_get_type(rmsg) == CMD_PUBLISH);
		// nng_msg_free(rmsg);

		nng_msleep(100);
		// client send unsub msg
		transtest_mqtt_unsub_send(tt->reqsock, client, true);
		nng_msleep(100);
		nng_ctx_recv(work->ctx, work->aio);
		So((rmsg = nng_aio_get_msg(work->aio)) != NULL);
		So(nng_msg_get_type(rmsg) == CMD_UNSUBSCRIBE);
		So((work->unsub_pkt = nng_alloc(sizeof(packet_unsubscribe))) != NULL);
		work->msg = rmsg;
		So(decode_unsub_msg(work) == 0);
		So(encode_unsuback_msg(rmsg, work) == 0);
		nng_aio_set_msg(work->aio, rmsg);
		nng_ctx_send(work->ctx, work->aio);
		nng_aio_finish(work->aio, 0);

		// nng_msleep(1000);
		// printf("qqqqqqqqqqqqq\n");
		// So(nng_recvmsg(tt->reqsock, &msg, 0) == 0);
		// printf("qqqqqqqqqqqqq\n");
		// So(msg != NULL);
		// printf("qqqqqqqqqqqqq\n");
		// So(nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_SUBACK);

		conn_param_free(rcp);
		nng_msg_free(msg);
		// nmq_broker will check connmsg when connection is
		// about to close, so we close the socket in advance
		// here to aviod heap-use-after-free.
		nng_close(tt->repsock);
		conn_param_free(cp);
		nng_mqtt_client_free(client, true);
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
mqtt_broker_trantest_test(const char *addr)
{
	trantest tt;

	memset(&tt, 0, sizeof(tt));
	Convey("MQTT broker given transport", {
		mqtt_broker_trantest_init(&tt, addr);

		Reset({ trantest_fini(&tt); });

		// trantest_scheme(&tt);
		// trantest_conn_refused(&tt);
		// trantest_duplicate_listen(&tt);
		// trantest_listen_accept(&tt);
		trantest_mqtt_broker_send_recv(&tt);
		// trantest_mqttv5_sub_pub(&tt);
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
