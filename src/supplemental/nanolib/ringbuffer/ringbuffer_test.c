#include "nng/supplemental/nanolib/ringbuffer.h"
#include "nng/mqtt/mqtt_client.h"
#include <nuts.h>

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

void test_ringBuffer_init(void)
{
	ringBuffer_t *rb;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);
	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

void test_ringBuffer_release(void)
{
	NUTS_TRUE(ringBuffer_release(NULL) != 0);

	return;
}

void test_ringBuffer_enqueue(void)
{
	ringBuffer_t *rb;
	nng_msg *tmp;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	/* Ring buffer is full, enqueue failed */
	tmp = alloc_pub_msg("topic1");
	NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) != 0);
	nng_msg_free(tmp);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	/* Allow overwrite */

	NUTS_TRUE(ringBuffer_init(&rb, 10, 1, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	/* Ring buffer is full, but overwrite is allowed, so it will be successful */
	tmp = alloc_pub_msg("topic1");
	NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

void test_ringBuffer_dequeue(void)
{
	ringBuffer_t *rb;
	nng_msg *tmp;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);

	/* Ring buffer is empty, dequeue failed */
	NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) != 0);

	/* Enqueue and dequeue normally */
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) != 0);

	/* Enqueue and dequeue abnormally */
	for (int i = 0; i < 12; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		if (i < 10) {
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
		} else {
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) != 0);
			nng_msg_free(tmp);
		}
	}

	NUTS_TRUE(rb->size == 10);

	for (int i = 0; i < 3; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_msg_free(tmp);
	}

	for (int i = 0; i < 5; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		if (i < 3) {
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
		} else {
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) != 0);
			nng_msg_free(tmp);
		}
	}

	NUTS_TRUE(rb->size == 10);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

static int matchTimes = 0;
static int targetTimes = 0;

static int match_cb(ringBuffer_t *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	matchTimes++;
	return 0;
}

static int match_fail_cb(ringBuffer_t *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	matchTimes++;
	return -1;
}

static int target_cb(ringBuffer_t *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	targetTimes++;
	return 0;
}

static int target_fail_cb(ringBuffer_t *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	targetTimes++;
	return -1;
}

void test_ringBuffer_rule()
{
	ringBuffer_t *rb;
	nng_msg *tmp;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);

	NUTS_TRUE(ringBuffer_add_rule(NULL, match_cb, target_cb, ENQUEUE_IN_HOOK) != 0);
	NUTS_TRUE(ringBuffer_add_rule(rb, NULL, target_cb, ENQUEUE_IN_HOOK) != 0);
	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, NULL, ENQUEUE_IN_HOOK) != 0);
	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, 0) != 0);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, ENQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 10);
	NUTS_TRUE(targetTimes == 10);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(matchTimes == 10);
	NUTS_TRUE(targetTimes == 10);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, ENQUEUE_OUT_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 30);
	NUTS_TRUE(targetTimes == 30);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(matchTimes == 30);
	NUTS_TRUE(targetTimes == 30);


	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, DEQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 50);
	NUTS_TRUE(targetTimes == 50);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(matchTimes == 60);
	NUTS_TRUE(targetTimes == 60);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, DEQUEUE_OUT_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 80);
	NUTS_TRUE(targetTimes == 80);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(matchTimes == 100);
	NUTS_TRUE(targetTimes == 100);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_fail_cb, target_cb, ENQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 130);
	NUTS_TRUE(targetTimes == 120);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(matchTimes == 150);
	NUTS_TRUE(targetTimes == 140);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_fail_cb, ENQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1) != 0);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(matchTimes == 180);
	NUTS_TRUE(targetTimes == 160);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

NUTS_TESTS = {
	{ "Ring buffer init test", test_ringBuffer_init },
	{ "Ring buffer release test", test_ringBuffer_release },
	{ "Ring buffer enqueue test", test_ringBuffer_enqueue },
	{ "Ring buffer dequeue test", test_ringBuffer_dequeue },
	{ "Ring buffer rule test", test_ringBuffer_rule },
	{ NULL, NULL },
};
