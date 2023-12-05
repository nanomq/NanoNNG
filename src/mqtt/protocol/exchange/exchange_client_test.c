#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/exchange/exchange_client.h"
#include "core/defs.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)
//
// Publish a message to the given topic and with the given QoS.
void
client_publish(nng_socket sock, const char *topic, uint8_t *payload,
    uint32_t payload_len, uint8_t qos, bool verbose)
{
	int rv;
	int *key = nng_alloc(sizeof(int));
	*key = 1;

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

	nni_aio_set_prov_data(aio, key);
	nni_aio_set_msg(aio, pubmsg);

	nng_send_aio(sock, aio);
	nng_aio_wait(aio);
	nng_aio_free(aio);
}

void
test_exchange_client(void)
{
	int rv = 0;
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

	nng_socket_set_ptr(sock, NNG_OPT_EXCHANGE_ADD, ex);
	nng_socket_get_ptr(sock, NNG_OPT_EXCHANGE_GET_EX_QUEUE, &ex_queue);
	NUTS_TRUE(ex_queue != NULL);

	rv = exchange_queue_get_ringBuffer(ex_queue, "ringBuffer1", &rb);
	NUTS_TRUE(rv == 0 && rb != NULL);

	rb = NULL;
	rv = exchange_queue_get_ringBuffer(ex_queue, "ringBuffer2", &rb);
	NUTS_TRUE(rv != 0 && rb == NULL);

	client_publish(sock, "topic1", NULL, 0, 0, 0);

	nng_msleep(200);

	return;
}

NUTS_TESTS = {
	{ "Exchange client test", test_exchange_client },
	{ NULL, NULL },
};
