#include "nng/exchange/exchange.h"
#include "nng/mqtt/mqtt_client.h"
#include <nuts.h>

#define EX_NAME	"exchange1"
#define UNUSED(x) ((void) x)

static inline void free_msg_list(nng_msg **msgList, nng_msg *msg, int *lenp, int freeMsg)
{
	for (int i = 0; i < *lenp; i++) {
		if (freeMsg) {
			nng_msg_free(msgList[i]);
		}
	}

	if (msg != NULL) {
		nng_msg_free(msg);
	}
	if (msgList != NULL) {
		nng_free(msgList, sizeof(nng_msg *) * (*lenp));
	}
	if (lenp != NULL) {
		nng_free(lenp, sizeof(int));
	}
}

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
	nng_mqtt_msg_encode(pubmsg);

	return pubmsg;
}

void test_exchange_init(void)
{
	exchange_t *ex = NULL;
	char **ringBufferName;
	unsigned int caps = 10000;
	uint8_t fullOps = RB_FULL_NONE;

	ringBufferName = nng_alloc(1 * sizeof(char *));
	for (int i = 0; i < 1; i++) {
		ringBufferName[i] = nng_alloc(100 * sizeof(char));
	}

	strcpy(ringBufferName[0], "ringBuffer1");

	NUTS_TRUE(exchange_init(NULL, NULL, "topic1", &caps, ringBufferName, &fullOps, 1) != 0);
	NUTS_TRUE(exchange_init(&ex, NULL, "topic1", &caps, ringBufferName, &fullOps, 1) != 0);

	NUTS_TRUE(exchange_init(&ex, EX_NAME, "topic1", &caps, ringBufferName, &fullOps, 1) == 0);
	NUTS_TRUE(ex != NULL);
	NUTS_TRUE(ex->rb_count != 0);
	NUTS_TRUE(exchange_release(ex) == 0);

	for (int i = 0; i < 1; i++) {
		nng_free(ringBufferName[i], sizeof(*ringBufferName[i]));
	}
	nng_free(ringBufferName, sizeof(*ringBufferName));

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
	unsigned int caps = 10;

	ringBufferName = nng_alloc(1 * sizeof(char *));
	for (int i = 0; i < 1; i++) {
		ringBufferName[i] = nng_alloc(100 * sizeof(char));
	}
	strcpy(ringBufferName[0], "ringBuffer1");

	char topic[100] = "topic1";
	uint8_t fullOps = RB_FULL_RETURN;
	NUTS_TRUE(exchange_init(&ex, EX_NAME, "topic1", &caps, ringBufferName, &fullOps, 1) == 0);
	NUTS_TRUE(ex != NULL);
	NUTS_TRUE(ex->rb_count == 1);

	for (int i = 0; i < 1; i++) {
		nng_free(ringBufferName[i], *ringBufferName[i]);
	}
	nng_free(ringBufferName, sizeof(ringBufferName));

	nng_msg *msg = NULL;
	for (int i = 0; i < 10; i++) {
		msg = alloc_pub_msg((char *)&topic);
		NUTS_TRUE(msg != NULL);
		NUTS_TRUE(exchange_handle_msg(ex, i, (void *)msg, NULL) == 0);
	}

	nng_aio *aio = NULL;
	nng_aio_alloc(&aio, NULL, NULL);
	NUTS_TRUE(aio != NULL);
	nng_aio_begin(aio);

	msg = alloc_pub_msg((char *)&topic);
	NUTS_TRUE(msg != NULL);
	NUTS_TRUE(exchange_handle_msg(ex, 1, (void *)msg, aio) == 0);

	/* when ringbuffer is full, get all msgs from ringbuffer and clean up */
	nng_msg **msgList = nng_aio_get_prov_data(aio);
	NUTS_TRUE(msgList != NULL);

	msg = nng_aio_get_msg(aio);
	NUTS_TRUE(msg != NULL);

	int *listLen = nng_msg_get_proto_data(msg);
	NUTS_TRUE(listLen != NULL);
	NUTS_TRUE(*listLen == 10);

	free_msg_list(msgList, msg, listLen, 1);

	nng_aio_finish(aio, 0);
	nng_aio_free(aio);
	NUTS_TRUE(exchange_release(ex) == 0);
}

NUTS_TESTS = {
	{ "Exchange init test", test_exchange_init },
	{ "Exchange release test", test_exchange_release },
	{ "Exchange ringBuffer test", test_exchange_ringBuffer },
	{ NULL, NULL },
};
