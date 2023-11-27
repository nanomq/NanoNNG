#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/exchange/exchange_client.h"
#include "core/defs.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)
//
// Publish a message to the given topic and with the given QoS.
int
client_publish(nng_socket sock, const char *topic, uint8_t *payload,
    uint32_t payload_len, uint8_t qos, bool verbose)
{
	int rv;

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

	rv = nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK);

	return rv;
}

void
test_exchange_client(void)
{
	nng_socket sock;
	exchange_t *ex;

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

	NUTS_TRUE(client_publish(sock, "topic1", NULL, 0, 0, 0) == 0);

	nng_msleep(200);

	return;
}

NUTS_TESTS = {
	{ "Exchange client test", test_exchange_client },
	{ NULL, NULL },
};
