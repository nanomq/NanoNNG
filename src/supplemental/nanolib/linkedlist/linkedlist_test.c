#include "nng/supplemental/nanolib/linkedlist.h"
#include <nuts.h>

void test_linkedList_init(void)
{
	struct linkedList *list;

	NUTS_TRUE(linkedList_init(&list, 10, 0, -1) == 0);
	NUTS_TRUE(list != NULL);
	NUTS_TRUE(linkedList_release(list) == 0);

	return;
}

void test_linkedList_release(void)
{
	NUTS_TRUE(linkedList_release(NULL) != 0);

	return;
}

void test_linkedList_enqueue(void)
{
	struct linkedList *list;
	int *tmp;

	NUTS_TRUE(linkedList_init(&list, 10, 0, -1) == 0);
	NUTS_TRUE(list != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(linkedList_enqueue(list, tmp, -1) == 0);
	}

	/* linked list is full, enqueue failed */
	tmp = nng_alloc(sizeof(int));
	NUTS_TRUE(linkedList_enqueue(list, tmp, -1) != 0);
	nng_free(tmp, sizeof(int));

	NUTS_TRUE(linkedList_release(list) == 0);

	/* Allow overwrite */

	NUTS_TRUE(linkedList_init(&list, 10, 1, -1) == 0);
	NUTS_TRUE(list != NULL);

	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(linkedList_enqueue(list, tmp, -1) == 0);
	}

	/* linked list is full, but overwrite is allowed, so it will be successful */
	tmp = nng_alloc(sizeof(int));
	NUTS_TRUE(linkedList_enqueue(list, tmp, -1) == 0);

	NUTS_TRUE(linkedList_release(list) == 0);

	return;
}

void test_linkedList_dequeue(void)
{
	struct linkedList *list;
	int *tmp;

	NUTS_TRUE(linkedList_init(&list, 10, 0, -1) == 0);
	NUTS_TRUE(list != NULL);

	/* linked list is empty, dequeue failed */
	NUTS_TRUE(linkedList_dequeue(list, (void **)&tmp) != 0);

	/* Enqueue and dequeue normally */
	for (int i = 0; i < 10; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		NUTS_TRUE(linkedList_enqueue(list, tmp, -1) == 0);
	}

	for (int i = 0; i < 10; i++) {
		NUTS_TRUE(linkedList_dequeue(list, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		NUTS_TRUE(*tmp == i);
		nng_free(tmp, sizeof(int));
	}

	NUTS_TRUE(linkedList_dequeue(list, (void **)&tmp) != 0);

	/* Enqueue and dequeue abnormally */
	for (int i = 0; i < 12; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		if (i < 10) {
			NUTS_TRUE(linkedList_enqueue(list, tmp, -1) == 0);
		} else {
			NUTS_TRUE(linkedList_enqueue(list, tmp, -1) != 0);
			nng_free(tmp, sizeof(int));
		}
	}

	NUTS_TRUE(list->size == 10);

	for (int i = 0; i < 3; i++) {
		NUTS_TRUE(linkedList_dequeue(list, (void **)&tmp) == 0);
		NUTS_TRUE(tmp != NULL);
		nng_free(tmp, sizeof(int));
	}

	for (int i = 0; i < 5; i++) {
		tmp = nng_alloc(sizeof(int));
		NUTS_TRUE(tmp != NULL);
		*tmp = i;
		if (i < 3) {
			NUTS_TRUE(linkedList_enqueue(list, tmp, -1) == 0);
		} else {
			NUTS_TRUE(linkedList_enqueue(list, tmp, -1) != 0);
			nng_free(tmp, sizeof(int));
		}
	}

	NUTS_TRUE(list->size == 10);

	NUTS_TRUE(linkedList_release(list) == 0);

	return;
}

NUTS_TESTS = {
	{ "linked list init test", test_linkedList_init },
	{ "linked list release test", test_linkedList_release },
	{ "linked list enqueue test", test_linkedList_enqueue },
	{ "linked list dequeue test", test_linkedList_dequeue },
	{ NULL, NULL },
};
