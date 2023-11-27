#include "nng/exchange/exchange.h"
#include "nng/mqtt/mqtt_client.h"
#include <nuts.h>

#define EX_NAME	"exchange1"
#define UNUSED(x) ((void) x)

nng_msg *alloc_pub_msg(const char *topic)
{
	// create a PUBLISH message
	nng_msg *pubmsg;
	nng_mqtt_msg_alloc(&pubmsg, 0);
	nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(pubmsg, 0);
	nng_mqtt_msg_set_publish_retain(pubmsg, 0);
	nng_mqtt_msg_set_publish_topic(pubmsg, topic);
	nng_mqtt_msg_set_publish_topic_len(pubmsg, strlen(topic));

	return pubmsg;
}

void test_exchange_init(void)
{
	exchange_t *ex = NULL;
	char **ringBufferName;
	int caps = 10000;

	ringBufferName = nng_alloc(1 * sizeof(char *));
	for (int i = 0; i < 1; i++) {
		ringBufferName[i] = nng_alloc(100 * sizeof(char));
	}

	strcpy(ringBufferName[0], "ringBuffer1");

	NUTS_TRUE(exchange_init(NULL, NULL, "topic1", &caps, ringBufferName, 1) != 0);
	NUTS_TRUE(exchange_init(&ex, NULL, "topic1", &caps, ringBufferName, 1) != 0);

	NUTS_TRUE(exchange_init(&ex, EX_NAME, "topic1", &caps, ringBufferName, 1) == 0);
	NUTS_TRUE(ex != NULL);
	NUTS_TRUE(ex->rb_count != 0);
	NUTS_TRUE(exchange_release(ex) == 0);

	for (int i = 0; i < 1; i++) {
		nng_free(ringBufferName[i], *ringBufferName[i]);
	}
	nng_free(ringBufferName, strlen(ringBufferName));

	return;
}

void test_exchange_release(void)
{
	NUTS_TRUE(exchange_release(NULL) != 0);

	return;
}

void test_exchange_ringBuffer(void)
{
	exchange_t *ex = NULL;
	char **ringBufferName;
	int caps = 1;

	ringBufferName = nng_alloc(1 * sizeof(char *));
	for (int i = 0; i < 1; i++) {
		ringBufferName[i] = nng_alloc(100 * sizeof(char));
	}
	strcpy(ringBufferName[0], "ringBuffer1");

	NUTS_TRUE(exchange_init(&ex, EX_NAME, "topic1", &caps, &ringBufferName, 1) == 0);
	NUTS_TRUE(ex != NULL);
	NUTS_TRUE(ex->rb_count == 1);

	for (int i = 0; i < 1; i++) {
		nng_free(ringBufferName[i], *ringBufferName[i]);
	}
	nng_free(ringBufferName, sizeof(ringBufferName));

	nng_msg *msg = NULL;
	msg = alloc_pub_msg("topic1");
	NUTS_TRUE(msg != NULL);

	NUTS_TRUE(exchange_handle_msg(ex, (void *)msg) == 0);
	NUTS_TRUE(exchange_release(ex) == 0);
}

NUTS_TESTS = {
	{ "Exchange init test", test_exchange_init },
	{ "Exchange release test", test_exchange_release },
	{ "Exchange ringBuffer test", test_exchange_ringBuffer },
	{ NULL, NULL },
};
