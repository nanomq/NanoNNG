#include "nng/supplemental/nanolib/ringbuffer.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

void test_ringBuffer_init(void)
{
	struct ringBuffer *rb;

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
	struct ringBuffer *rb;
	int *tmp;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	/* Ring buffer is full, enqueue failed */
	tmp = nng_alloc(sizeof(int));
	NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) != 0);
	nng_free(tmp, sizeof(int));

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	/* Allow overwrite */

	NUTS_TRUE(ringBuffer_init(&rb, 10, 1, -1) == 0);
	NUTS_TRUE(rb != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	/* Ring buffer is full, but overwrite is allowed, so it will be successful */
	tmp = nng_alloc(sizeof(int));
	NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

void test_ringBuffer_dequeue(void)
{
	struct ringBuffer *rb;
	int *tmp;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);

	/* Ring buffer is empty, dequeue failed */
	NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) != 0);

	/* Enqueue and dequeue normally */
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(*tmp == i);
		nng_free(tmp, sizeof(int));
	}

	NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) != 0);

	/* Enqueue and dequeue abnormally */
	for (int i = 0; i < 12; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		if (i < 10) {
			NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
		} else {
			NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) != 0);
			nng_free(tmp, sizeof(int));
		}
	}

	NUTS_TRUE(rb->size == 10);

	for (int i = 0; i < 3; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_free(tmp, sizeof(int));
	}

	for (int i = 0; i < 5; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		if (i < 3) {
			NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
		} else {
			NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) != 0);
			nng_free(tmp, sizeof(int));
		}
	}

	NUTS_TRUE(rb->size == 10);

	NUTS_TRUE(ringBuffer_release(rb) == 0);

	return;
}

static int matchTimes = 0;
static int targetTimes = 0;

static int match_cb(struct ringBuffer *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	matchTimes++;
	return 0;
}

static int match_fail_cb(struct ringBuffer *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	matchTimes++;
	return -1;
}

static int target_cb(struct ringBuffer *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	targetTimes++;
	return 0;
}

static int target_fail_cb(struct ringBuffer *rb, void *data, int flag)
{
	UNUSED(rb);
	UNUSED(data);
	UNUSED(flag);
	targetTimes++;
	return -1;
}

void test_ringBuffer_rule()
{
	struct ringBuffer *rb;
	int *tmp;

	NUTS_TRUE(ringBuffer_init(&rb, 10, 0, -1) == 0);
	NUTS_TRUE(rb != NULL);

	NUTS_TRUE(ringBuffer_add_rule(NULL, match_cb, target_cb, ENQUEUE_IN_HOOK) != 0);
	NUTS_TRUE(ringBuffer_add_rule(rb, NULL, target_cb, ENQUEUE_IN_HOOK) != 0);
	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, NULL, ENQUEUE_IN_HOOK) != 0);
	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, 0) != 0);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, ENQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 10);
	NUTS_TRUE(targetTimes == 10);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(*tmp == i);
		nng_free(tmp, sizeof(int));
	}

	NUTS_TRUE(matchTimes == 10);
	NUTS_TRUE(targetTimes == 10);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, ENQUEUE_OUT_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 30);
	NUTS_TRUE(targetTimes == 30);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(*tmp == i);
		nng_free(tmp, sizeof(int));
	}

	NUTS_TRUE(matchTimes == 30);
	NUTS_TRUE(targetTimes == 30);


	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, DEQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 50);
	NUTS_TRUE(targetTimes == 50);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(*tmp == i);
		nng_free(tmp, sizeof(int));
	}

	NUTS_TRUE(matchTimes == 60);
	NUTS_TRUE(targetTimes == 60);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_cb, DEQUEUE_OUT_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 80);
	NUTS_TRUE(targetTimes == 80);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(*tmp == i);
		nng_free(tmp, sizeof(int));
	}

	NUTS_TRUE(matchTimes == 100);
	NUTS_TRUE(targetTimes == 100);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_fail_cb, target_cb, ENQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) == 0);
	}

	NUTS_TRUE(matchTimes == 130);
	NUTS_TRUE(targetTimes == 120);

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(ringBuffer_dequeue(rb, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(*tmp == i);
		nng_free(tmp, sizeof(int));
	}

	NUTS_TRUE(matchTimes == 150);
	NUTS_TRUE(targetTimes == 140);

	NUTS_TRUE(ringBuffer_add_rule(rb, match_cb, target_fail_cb, ENQUEUE_IN_HOOK) == 0);
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(ringBuffer_enqueue(rb, tmp, -1) != 0);
		nng_free(tmp, sizeof(int));
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
