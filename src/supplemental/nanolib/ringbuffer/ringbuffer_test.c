#include "nng/supplemental/nanolib/ringbuffer.h"
#include "nng/exchange/stream/stream.h"
#include "nng/mqtt/mqtt_client.h"
#include <nuts.h>
#include <string.h>

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
	nng_mqtt_msg_encode(pubmsg);

	return pubmsg;
}

void test_ringBuffer_init(void)
{
	ringBuffer_t *rb;

	NUTS_TRUE(ringBuffer_init(&rb, 10, RB_FULL_NONE, -1) == 0);
	NUTS_TRUE(rb != NULL);
	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

void test_ringBuffer_release(void)
{
	NUTS_TRUE(ringBuffer_release(NULL) != 0);

	return;
}

static inline void free_msg_list(nng_msg **msgList, nng_msg *msg, uint32_t *lenp, int freeMsg)
{
	for (uint32_t i = 0; i < *lenp; i++) {
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
		nng_free(lenp, sizeof(uint32_t));
	}
}

void test_ringBuffer_enqueue(void)
{
	ringBuffer_t *rb;
	nng_msg *tmp;

	NUTS_TRUE(ringBuffer_init(&rb, 10, RB_FULL_NONE, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
	}

	/* Ring buffer is full, enqueue failed */
	tmp = alloc_pub_msg("topic1");
	NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) != 0);
	nng_msg_free(tmp);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	/* RB_FULL_DROP */
	NUTS_TRUE(ringBuffer_init(&rb, 10, RB_FULL_DROP, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
	}

	/* ring buffer is full, drop all msgs and enqueue new */
	tmp = alloc_pub_msg("topic1");
	NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
	NUTS_TRUE(rb->size == 1);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	/* RB_FULL_FILE */
	NUTS_TRUE(ringBuffer_init(&rb, 10, RB_FULL_FILE, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
	}

	/* ring buffer is full, write msgs to file and enqueue new */
	tmp = alloc_pub_msg("topic1");
	NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) != 0);
	nng_msg_free(tmp);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	/* RB_FULL_RETURN */
	NUTS_TRUE(ringBuffer_init(&rb, 10, RB_FULL_RETURN, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
	}

	/* Ring buffer is full, but overwrite is allowed, so it will be successful */
	nng_aio *aio = NULL;
	nng_aio_alloc(&aio, NULL, NULL);
	NUTS_TRUE(aio != NULL);
	nng_aio_begin(aio);

	tmp = alloc_pub_msg("topic1");
	NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, aio) == 0);

	/* when ringbuffer is full, get all msgs from ringbuffer and clean up */
	nng_msg **msgList = nng_aio_get_prov_data(aio);
	NUTS_TRUE(msgList != NULL);

	nng_msg *msg = nng_aio_get_msg(aio);
	NUTS_TRUE(msg != NULL);

	uint32_t *listLen = nng_msg_get_proto_data(msg);
	NUTS_TRUE(listLen != NULL);
	NUTS_TRUE(*listLen == 10);

	free_msg_list(msgList, msg, listLen, 1);

	nng_aio_finish(aio, 0);
	nng_aio_free(aio);

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
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
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
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
		} else {
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) != 0);
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
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
		} else {
			NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) != 0);
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
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
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
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
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
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
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
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
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
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) == 0);
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
		NUTS_TRUE(ringBuffer_enqueue(rb, 1, tmp, -1, NULL) != 0);
		nng_msg_free(tmp);
	}

	NUTS_TRUE(matchTimes == 180);
	NUTS_TRUE(targetTimes == 160);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

void test_ringBuffer_search_msg_by_key()
{
	ringBuffer_t *rb = NULL;
	nng_msg *tmp = NULL;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, i, tmp, -1, NULL) == 0);
	}
	tmp = NULL;
	NUTS_TRUE(ringBuffer_search_msg_by_key(NULL, 0, &tmp) == -1);
	NUTS_TRUE(ringBuffer_search_msg_by_key(rb, 0, NULL) == -1);
	NUTS_TRUE(ringBuffer_search_msg_by_key(rb, 11, &tmp) == -1);

	NUTS_TRUE(ringBuffer_search_msg_by_key(rb, 0, &tmp) == 0);
	NUTS_TRUE(tmp != NULL);

	NUTS_TRUE(ringBuffer_release(rb) == 0);
}

void test_ringBuffer_search_msgs_by_key()
{
	ringBuffer_t *rb = NULL;
	nng_msg *tmp = NULL;
	nng_msg **msgList = NULL;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, i, tmp, -1, NULL) == 0);
	}

	NUTS_TRUE(ringBuffer_search_msgs_by_key(rb, 0, 11, &msgList) == -1);
	NUTS_TRUE(ringBuffer_search_msgs_by_key(NULL, 0, 10, &msgList) == -1);
	NUTS_TRUE(ringBuffer_search_msgs_by_key(rb, -1, 10, &msgList) == -1);
	NUTS_TRUE(ringBuffer_search_msgs_by_key(rb, 0, -1, &msgList) == -1);
	NUTS_TRUE(ringBuffer_search_msgs_by_key(rb, 0, 10, NULL) == -1);

	uint32_t *lenp = NULL;
	lenp = nng_alloc(sizeof(uint32_t));
	NUTS_TRUE(ringBuffer_search_msgs_by_key(rb, 0, 10, &msgList) == 0);
	*lenp = 10;
	free_msg_list(msgList, NULL, lenp, 0);


	NUTS_TRUE(ringBuffer_search_msgs_by_key(rb, 5, 10, &msgList) == 0);
	lenp = nng_alloc(sizeof(uint32_t));
	*lenp = 10;
	free_msg_list(msgList, NULL, lenp, 0);

	NUTS_TRUE(ringBuffer_release(rb) == 0);
}

void test_ringBuffer_search_msgs_fuzz()
{
	ringBuffer_t *rb = NULL;
	nng_msg *tmp = NULL;
	nng_msg **msgList = NULL;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, i * 10, tmp, -1, NULL) == 0);
	}

	for (int i = 0; i < 100; i++) {
		int start = nng_random() % 100;
		int end = nng_random() % 100;
		if (start > end) {
			int tmpstart = start;
			start = end;
			end = tmpstart;
		}
		uint32_t *lenp = nng_alloc(sizeof(uint32_t));
		int ret;
		ret = ringBuffer_search_msgs_fuzz(rb, start, end, lenp, &msgList);
		if (start / 10 == end / 10 && start % 10 != 0 && end % 10 != 0) {
			NUTS_TRUE(ret == -1);
			nng_free(lenp, sizeof(uint32_t));
			continue;
		} else {
			NUTS_TRUE(ret == 0);
		}
		NUTS_TRUE(*lenp == (uint32_t)((end / 10  - (start % 10 == 0 ? start / 10 : start / 10 + 1)) + 1));
		free_msg_list(msgList, NULL, lenp, 0);
	}

	/* After the ring wraps, logical order is no longer the physical index. */
	for (int i = 0; i < 3; i++) {
		void *dropped = NULL;
		NUTS_TRUE(ringBuffer_dequeue(rb, &dropped) == 0);
		nng_msg_free((nng_msg *) dropped);
	}
	for (int i = 0; i < 3; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, 100 + (uint64_t) i * 10, tmp, -1, NULL) == 0);
	}
	NUTS_TRUE(rb->head != 0);
	{
		uint32_t *lenp = nng_alloc(sizeof(uint32_t));
		msgList = NULL;
		NUTS_TRUE(ringBuffer_search_msgs_fuzz(rb, 100, 120, lenp, &msgList) == 0);
		NUTS_TRUE(*lenp == 3);
		free_msg_list(msgList, NULL, lenp, 0);
	}

	NUTS_TRUE(ringBuffer_release(rb) == 0);

}

void test_ringBuffer_get_and_clean_up()
{
	ringBuffer_t *rb = NULL;
	nng_msg *tmp = NULL;
	nng_msg **msgList = NULL;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);
	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, i * 10, tmp, -1, NULL) == 0);
	}

	unsigned int *lenp = nng_alloc(sizeof(unsigned int));
	int ret;
	*lenp = 10;
	ret = ringBuffer_get_and_clean_msgs(rb, lenp, &msgList);
	NUTS_TRUE(ret == 0);
	NUTS_TRUE(*lenp == 10);
	free_msg_list(msgList, NULL, lenp, 1);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, i * 20, tmp, -1, NULL) == 0);
	}

	lenp = nng_alloc(sizeof(unsigned int));
	*lenp = 0;
	ret = ringBuffer_get_and_clean_msgs(rb, lenp, &msgList);
	NUTS_TRUE(ret == 0);
	NUTS_TRUE(*lenp == 10);
	free_msg_list(msgList, NULL, lenp, 1);

	for (int i = 0; i < 10; i++) {
		tmp = alloc_pub_msg("topic1");
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, i * 30, tmp, -1, NULL) == 0);
	}

	lenp = nng_alloc(sizeof(unsigned int));
	*lenp = 0;
	ret = ringBuffer_get_and_clean_msgs(rb, lenp, &msgList);
	NUTS_TRUE(ret == 0);
	NUTS_TRUE(*lenp == 10);
	free_msg_list(msgList, NULL, lenp, 1);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

}

void test_ringBuffer_fuzz_keeps_mqtt_payload(void)
{
	ringBuffer_t *rb = NULL;
	nng_msg *msg = NULL;
	nng_msg **list = NULL;
	uint32_t count = 0;
	uint32_t plen = 0;
	uint8_t *got = NULL;
	const uint8_t payload[] = "keep-me";

	NUTS_TRUE(ringBuffer_init(&rb, 4, RB_FULL_NONE, -1) == 0);
	NUTS_TRUE(nng_mqtt_msg_alloc(&msg, 0) == 0);
	nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(msg, 0);
	nng_mqtt_msg_set_publish_retain(msg, 0);
	nng_mqtt_msg_set_publish_topic(msg, "t/1");
	nng_mqtt_msg_set_publish_topic_len(msg, 3);
	NUTS_TRUE(nng_mqtt_msg_set_publish_payload(msg, (uint8_t *) payload, 7) == 0);
	NUTS_TRUE(nng_mqtt_msg_encode(msg) == 0);
	nng_msg_set_timestamp(msg, 42);
	NUTS_TRUE(ringBuffer_enqueue(rb, 42, msg, -1, NULL) == 0);

	NUTS_TRUE(ringBuffer_search_msgs_fuzz(rb, 42, 42, &count, &list) == 0);
	NUTS_TRUE(count == 1);
	NUTS_TRUE(list != NULL);
	got = stream_msg_payload(list[0], &plen);
	NUTS_TRUE(got != NULL);
	NUTS_TRUE(plen == 7);
	NUTS_TRUE(memcmp(got, payload, 7) == 0);

	nng_free(list, sizeof(nng_msg *) * count);
	NUTS_TRUE(ringBuffer_release(rb) == 0);
}

/* Build a message the way hook_entry() does: plain nng_msg_alloc, header
 * and body copied from the broker's PUBLISH, payload_ptr rebased into the
 * copy. There is no MQTT proto data on it. */
static nng_msg *
alloc_hook_style_msg(const char *topic, const uint8_t *payload, uint32_t plen,
    uint64_t ts)
{
	nng_msg *src = NULL;
	nng_msg *msg = NULL;
	uint8_t  hdr[2] = { 0x30, 0 };
	uint16_t tlen   = (uint16_t) strlen(topic);
	uint8_t  tl[2]  = { (uint8_t) (tlen >> 8), (uint8_t) (tlen & 0xff) };
	uint8_t *body_ptr;
	ptrdiff_t offset;

	/* Source message as the broker holds it: body is topic + payload,
	 * payload_ptr points at the payload inside it. */
	if (nng_msg_alloc(&src, 0) != 0) {
		return NULL;
	}
	nng_msg_header_append(src, hdr, sizeof(hdr));
	nng_msg_append(src, tl, sizeof(tl));
	nng_msg_append(src, topic, tlen);
	nng_msg_append(src, payload, plen);
	nng_msg_set_payload_ptr(src, (uint8_t *) nng_msg_body(src) + 2 + tlen);

	nng_msg_alloc(&msg, 0);
	nng_msg_header_append(msg, nng_msg_header(src), nng_msg_header_len(src));
	nng_msg_append(msg, nng_msg_body(src), nng_msg_len(src));
	body_ptr = nng_msg_body(src);
	offset   = (ptrdiff_t) (nng_msg_payload_ptr(src) - body_ptr);
	nng_msg_set_payload_ptr(msg, (uint8_t *) nng_msg_body(msg) + offset);
	nng_msg_set_timestamp(msg, ts);
	nng_msg_free(src);
	return msg;
}

void test_ringBuffer_hook_style_msg_payload(void)
{
	ringBuffer_t *rb = NULL;
	nng_msg **list = NULL;
	nng_msg **flush = NULL;
	uint32_t count = 0;
	uint32_t *lenp = NULL;
	uint32_t plen = 99;
	uint8_t *got = NULL;
	const uint8_t p0[] = "hook-payload-0";
	const uint8_t p1[] = "hook-1";
	nng_msg *m0, *m1;

	m0 = alloc_hook_style_msg("a/b", p0, (uint32_t) strlen((char *) p0), 10);
	m1 = alloc_hook_style_msg("a/b", p1, (uint32_t) strlen((char *) p1), 20);
	NUTS_TRUE(m0 != NULL && m1 != NULL);
	NUTS_TRUE(nng_msg_get_proto_data(m0) == NULL);

	NUTS_TRUE(ringBuffer_init(&rb, 4, RB_FULL_NONE, -1) == 0);
	NUTS_TRUE(ringBuffer_enqueue(rb, 10, m0, -1, NULL) == 0);
	NUTS_TRUE(ringBuffer_enqueue(rb, 20, m1, -1, NULL) == 0);

	/* query path */
	NUTS_TRUE(ringBuffer_search_msgs_fuzz(rb, 10, 20, &count, &list) == 0);
	NUTS_TRUE(count == 2);
	got = stream_msg_payload(list[0], &plen);
	NUTS_TRUE(got != NULL);
	NUTS_TRUE(plen == strlen((char *) p0));
	NUTS_TRUE(memcmp(got, p0, plen) == 0);
	got = stream_msg_payload(list[1], &plen);
	NUTS_TRUE(got != NULL);
	NUTS_TRUE(plen == strlen((char *) p1));
	NUTS_TRUE(memcmp(got, p1, plen) == 0);
	nng_free(list, sizeof(nng_msg *) * count);

	/* flush path */
	lenp = nng_alloc(sizeof(uint32_t));
	*lenp = 0;
	NUTS_TRUE(ringBuffer_get_and_clean_msgs(rb, lenp, &flush) == 0);
	NUTS_TRUE(*lenp == 2);
	got = stream_msg_payload(flush[0], &plen);
	NUTS_TRUE(got != NULL);
	NUTS_TRUE(plen == strlen((char *) p0));
	NUTS_TRUE(memcmp(got, p0, plen) == 0);
	free_msg_list(flush, NULL, lenp, 1);

	NUTS_TRUE(ringBuffer_release(rb) == 0);
}

NUTS_TESTS = {
	{ "Ring buffer init test", test_ringBuffer_init },
	{ "Ring buffer release test", test_ringBuffer_release },
	{ "Ring buffer enqueue test", test_ringBuffer_enqueue },
	{ "Ring buffer dequeue test", test_ringBuffer_dequeue },
	{ "Ring buffer rule test", test_ringBuffer_rule },
	{ "Ring buffer search msg by key", test_ringBuffer_search_msg_by_key },
	{ "Ring buffer search msgs by key", test_ringBuffer_search_msgs_by_key },
	{ "Ring buffer search msgs fuzz", test_ringBuffer_search_msgs_fuzz },
	{ "Ring buffer fuzz keeps mqtt payload", test_ringBuffer_fuzz_keeps_mqtt_payload },
	{ "Ring buffer hook style msg payload", test_ringBuffer_hook_style_msg_payload },
	{ "Ring buffer get and clean up test", test_ringBuffer_get_and_clean_up},
	{ NULL, NULL },
};
