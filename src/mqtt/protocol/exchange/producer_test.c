#include "nng/exchange/producer.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

static int match_cb(void *msg)
{
	UNUSED(msg);
	return 0;
}

static int target_cb(void *msg)
{
	UNUSED(msg);
	return 0;
}

void test_producer_init(void)
{
	producer_t *p = NULL;

	NUTS_TRUE(producer_init(NULL, match_cb, target_cb) != 0);
	NUTS_TRUE(producer_init(&p, NULL, target_cb) != 0);
	NUTS_TRUE(producer_init(&p, match_cb, NULL) != 0);

	NUTS_TRUE(producer_init(&p, match_cb, target_cb) == 0);
	NUTS_TRUE(p != NULL);
	NUTS_TRUE(producer_release(p) == 0);

	return;
}

void test_producer_release(void)
{
	NUTS_TRUE(producer_release(NULL) != 0);

	return;
}

NUTS_TESTS = {
	{ "Producer init test", test_producer_init },
	{ "Producer release test", test_producer_release },
	{ NULL, NULL },
};
