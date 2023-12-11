#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/exchange/exchange_client.h"
#include "nng/exchange/exchange.h"
#include "core/defs.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)
//
// Publish a message to the given topic and with the given QoS.
void
client_publish(nng_socket sock, const char *topic, uint32_t key, uint8_t *payload,
    uint32_t payload_len, uint8_t qos, bool verbose)
{
	int rv;
	int *_key = nng_alloc(sizeof(int));
	*_key = key;

	UNUSED(verbose);
	// create a PUBLISH message
	nng_msg *pubmsg;
	nng_mqtt_msg_alloc(&pubmsg, 0);
	nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(pubmsg, 0);
	nng_mqtt_msg_set_publish_qos(pubmsg, qos);
	nng_mqtt_msg_set_publish_retain(pubmsg, 0);
	nng_mqtt_msg_set_publish_payload(
	    pubmsg, (uint8_t *) payload, payload_len);
	nng_mqtt_msg_set_publish_topic(pubmsg, topic);
	nng_mqtt_msg_set_publish_topic_len(pubmsg, strlen(topic));

	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));

	nni_aio_set_prov_data(aio, _key);
	nni_aio_set_msg(aio, pubmsg);

	nng_send_aio(sock, aio);
	nng_aio_wait(aio);
	nng_aio_free(aio);
}

void
test_exchange_client(void)
{
	int rv = 0;
	uint32_t key = 0;
	nng_socket sock;
	exchange_t *ex = NULL;
	nni_list *ex_queue = NULL;
	ringBuffer_t *rb = NULL;

	NUTS_TRUE(nng_exchange_client_open(&sock) == 0);

	char **ringBufferName;
	ringBufferName = nng_alloc(1 * sizeof(char *));
	for (int i = 0; i < 1; i++) {
		ringBufferName[i] = nng_alloc(100 * sizeof(char));
	}

	strcpy(ringBufferName[0], "ringBuffer1");

	int caps = 10000;

	NUTS_TRUE(exchange_init(&ex, "exchange1", "topic1", (void *)&caps, ringBufferName, 1) == 0);
	NUTS_TRUE(ex != NULL);

	for (int i = 0; i < 1; i++) {
		nng_free(ringBufferName[i], sizeof(*ringBufferName[i]));
	}
	nng_free(ringBufferName, sizeof(*ringBufferName));

	nng_socket_set_ptr(sock, NNG_OPT_EXCHANGE_BIND, ex);
	nng_socket_get_ptr(sock, NNG_OPT_EXCHANGE_GET_EX_QUEUE, &ex_queue);
	NUTS_TRUE(ex_queue != NULL);

	rv = exchange_queue_get_ringBuffer(ex_queue, "ringBuffer1", &rb);
	NUTS_TRUE(rv == 0 && rb != NULL);

	rb = NULL;
	rv = exchange_queue_get_ringBuffer(ex_queue, "ringBuffer2", &rb);
	NUTS_TRUE(rv != 0 && rb == NULL);

	key = 1;
	client_publish(sock, "topic1", key, NULL, 0, 0, 0);

	nni_msg *msg = NULL;
	nni_sock *nsock = NULL;

	rv = nni_sock_find(&nsock, sock.id);
	NUTS_TRUE(rv == 0 && nsock != NULL);
	nni_sock_rele(nsock);

	rv = exchange_client_get_msg_by_key(nni_sock_proto_data(nsock), key, &msg);
	NUTS_TRUE(rv == 0 && msg != NULL);

	nni_list *list = NULL;
	rv = exchange_client_get_msgs_by_key(nni_sock_proto_data(nsock), key, 1, &list);
	NUTS_TRUE(rv == 0 && list != NULL);

	ringBuffer_msgs_t *rb_msg = NULL;
	while (!nni_list_empty(list)) {
		rb_msg = nni_list_last(list);
		if (rb_msg != NULL) {
			nni_list_remove(list, rb_msg);
			nng_free(rb_msg, sizeof(*rb_msg));
		}
	}

	nng_free(list, sizeof(*list));
	list = NULL;

	/* Only one element in ringbuffer */
	rv = exchange_client_get_msgs_by_key(nni_sock_proto_data(nsock), key, 2, &list);
	NUTS_TRUE(rv == -1 && list == NULL);

	return;
}

NUTS_TESTS = {
	{ "Exchange client test", test_exchange_client },
	{ NULL, NULL },
};
