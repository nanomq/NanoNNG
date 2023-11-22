#include "nng/supplemental/nanolib/ringbuffer.h"
#include <nuts.h>

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

NUTS_TESTS = {
	{ "Ring buffer init test", test_ringBuffer_init },
	{ "Ring buffer release test", test_ringBuffer_release },
	{ "Ring buffer enqueue test", test_ringBuffer_enqueue },
	{ "Ring buffer dequeue test", test_ringBuffer_dequeue },
	{ NULL, NULL },
};
