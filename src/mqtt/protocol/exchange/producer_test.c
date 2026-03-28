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

static int match_result = 0;
static int target_result = 0;

static int tracking_match_cb(void *msg)
{
	UNUSED(msg);
	match_result++;
	return 0;
}

static int tracking_target_cb(void *msg)
{
	UNUSED(msg);
	target_result++;
	return 1;
}

void test_producer_callbacks_stored(void)
{
	producer_t *p = NULL;

	match_result = 0;
	target_result = 0;

	NUTS_TRUE(producer_init(&p, tracking_match_cb, tracking_target_cb) == 0);
	NUTS_TRUE(p != NULL);

	/* Verify function pointers are stored correctly */
	NUTS_TRUE(p->match == tracking_match_cb);
	NUTS_TRUE(p->target == tracking_target_cb);

	/* Invoke callbacks through the stored pointers */
	int m = p->match(NULL);
	NUTS_TRUE(m == 0);
	NUTS_TRUE(match_result == 1);

	int t = p->target(NULL);
	NUTS_TRUE(t == 1);
	NUTS_TRUE(target_result == 1);

	NUTS_TRUE(producer_release(p) == 0);
}

NUTS_TESTS = {
	{ "Producer init test", test_producer_init },
	{ "Producer release test", test_producer_release },
	{ "Producer callbacks stored correctly", test_producer_callbacks_stored },
	{ NULL, NULL },
};