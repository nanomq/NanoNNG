/*
 * NanoNNG Zephyr MQTT Client Demo — async state-machine style
 *
 * Follows the pattern of demo/mqtt_async:
 *   - AIO callback state machine (INIT → RECV → WAIT → SEND)
 *   - nng_dialer_create + nng_dialer_start for MQTT connect
 *   - nng_ctx_recv / nng_ctx_send for async I/O
 *   - connect / disconnect callbacks
 *
 * Build: west build -b qemu_x86 .
 * Run:   west build -t run
 *
 * NOTE: QEMU User Networking maps 10.0.2.2 to the host.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <nng/nng.h>
#include <nng/mqtt/mqtt_client.h>
#include <nng/protocol/mqtt/mqtt.h>
#include <nng/supplemental/util/platform.h>

// #define BROKER_URL  "mqtt-tcp://100.80.195.4:1883"
#define BROKER_URL  "mqtt-tcp://broker.emqx.io:1883"
#define CLIENT_ID   "zephyr-mqtt-demo"

#define SUB_TOPIC1  "/zephyr/msg/1"
#define SUB_TOPIC2  "/zephyr/msg/2"
#define PUB_TOPIC   "/zephyr/msg/transfer"

/* ===== work item ===== */

enum state { INIT, RECV, WAIT, SEND };

struct work {
	enum state  state;
	nng_aio *   aio;
	nng_msg *   msg;
	nng_ctx     ctx;
};

static void client_cb(void *arg);

static void
fatal(const char *m, int rv)
{
	printk("FATAL: %s: %s\n", m, nng_strerror(rv));
	while (1) k_sleep(K_SECONDS(1));
}

static struct work *
alloc_work(nng_socket sock)
{
	struct work *w;
	int rv;
	if ((w = nng_alloc(sizeof(*w))) == NULL)
		fatal("nng_alloc", NNG_ENOMEM);
	if ((rv = nng_aio_alloc(&w->aio, client_cb, w)) != 0)
		fatal("nng_aio_alloc", rv);
	if ((rv = nng_ctx_open(&w->ctx, sock)) != 0)
		fatal("nng_ctx_open", rv);
	w->state = INIT;
	w->msg   = NULL;
	return w;
}

/* ===== callbacks ===== */

static void
connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	printk("MQTT: connected\n");
}

static void
disconnect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	printk("MQTT: disconnected\n");
}

/* ===== AIO state machine ===== */

static void
client_cb(void *arg)
{
	struct work *w = arg;
	nng_msg *msg;
	int rv;

	switch (w->state) {

	case INIT:
		w->state = RECV;
		nng_ctx_recv(w->ctx, w->aio);
		break;

	case RECV:
		if ((rv = nng_aio_result(w->aio)) != 0) {
			printk("MQTT: recv err %s\n", nng_strerror(rv));
			w->state = RECV;
			nng_ctx_recv(w->ctx, w->aio);
			break;
		}
		w->msg   = nng_aio_get_msg(w->aio);
		w->state = WAIT;
		nng_sleep_aio(0, w->aio);
		break;

	case WAIT: {
		msg = w->msg;

		// only handle PUBLISH
		if (nng_mqtt_msg_get_packet_type(msg) != NNG_MQTT_PUBLISH) {
			nng_msg_free(msg);
			w->msg = NULL;
			w->state = RECV;
			nng_ctx_recv(w->ctx, w->aio);
			break;
		}

		uint32_t plen, tlen;
		uint8_t *payload =
		    nng_mqtt_msg_get_publish_payload(msg, &plen);
		const char *topic =
		    nng_mqtt_msg_get_publish_topic(msg, &tlen);

		printk("MQTT RECV: '%.*s' FROM: '%.*s'\n",
		    (int)plen, (const char *)payload, (int)tlen, topic);

		// echo payload to PUB_TOPIC
		uint8_t *send_data = nng_alloc(plen);
		memcpy(send_data, payload, plen);

		nng_msg_header_clear(msg);
		nng_msg_clear(msg);

		nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
		nng_mqtt_msg_set_publish_topic(msg, PUB_TOPIC);
		nng_mqtt_msg_set_publish_payload(msg, send_data, plen);
		nng_mqtt_msg_set_publish_qos(msg, 1);

		printk("MQTT SEND: '%.*s' TO: '%s'\n",
		    (int)plen, (const char *)send_data, PUB_TOPIC);

		nng_free(send_data, plen);
		nng_aio_set_msg(w->aio, msg);
		w->msg   = NULL;
		w->state = SEND;
		nng_ctx_send(w->ctx, w->aio);
		break;
	}

	case SEND:
		if ((rv = nng_aio_result(w->aio)) != 0)
			printk("MQTT: send err %s\n", nng_strerror(rv));
		if (w->msg) { nng_msg_free(w->msg); w->msg = NULL; }
		w->state = RECV;
		nng_ctx_recv(w->ctx, w->aio);
		break;
	}
}

/* ===== connect routine ===== */

static void
mqtt_connect(const char *url)
{
	nng_socket sock;
	nng_dialer dialer;
	struct work *worker;
	nng_msg *conn_msg, *sub_msg;
	int rv;

	if ((rv = nng_mqtt_client_open(&sock)) != 0)
		fatal("nng_mqtt_client_open", rv);
	printk("MQTT: socket opened\n");

	worker = alloc_work(sock);

	nng_mqtt_msg_alloc(&conn_msg, 0);
	nng_mqtt_msg_set_packet_type(conn_msg, NNG_MQTT_CONNECT);
	nng_mqtt_msg_set_connect_keep_alive(conn_msg, 60);
	nng_mqtt_msg_set_connect_clean_session(conn_msg, true);
	nng_mqtt_msg_set_connect_client_id(conn_msg, CLIENT_ID);
	nng_mqtt_msg_set_connect_proto_version(
	    conn_msg, MQTT_PROTOCOL_VERSION_v311);

	nng_mqtt_set_connect_cb(sock, connect_cb, NULL);
	nng_mqtt_set_disconnect_cb(sock, disconnect_cb, NULL);

	if ((rv = nng_dialer_create(&dialer, sock, url)) != 0)
		fatal("nng_dialer_create", rv);

	nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, conn_msg);
	if ((rv = nng_dialer_start(dialer, NNG_FLAG_ALLOC)) != 0){
        fatal("nng_dialer_start", rv);
    }
	printk("MQTT: dialer started for %s\n", url);

	// allow connection to settle
	nng_msleep(1000);

	nng_mqtt_topic_qos tq[] = {
		{ .qos = 1, .topic = { strlen(SUB_TOPIC1), (uint8_t *)SUB_TOPIC1 } },
		{ .qos = 1, .topic = { strlen(SUB_TOPIC2), (uint8_t *)SUB_TOPIC2 } },
	};
	size_t n = sizeof(tq) / sizeof(tq[0]);

	nng_mqtt_msg_alloc(&sub_msg, 0);
	nng_mqtt_msg_set_packet_type(sub_msg, NNG_MQTT_SUBSCRIBE);
	nng_mqtt_msg_set_subscribe_topics(sub_msg, tq, n);
	printk("MQTT: subscribing %zu topics\n", n);
	nng_sendmsg(sock, sub_msg, NNG_FLAG_ALLOC);

	client_cb(worker);  // kick state machine

	printk("MQTT: event loop running...\n\n");
	for (;;) nng_msleep(3600000);
}

void main(void)
{
	printk("\n=== NanoNNG Zephyr MQTT Client (async) ===\n");
	printk("Broker: %s\n", BROKER_URL);
	mqtt_connect(BROKER_URL);
}
