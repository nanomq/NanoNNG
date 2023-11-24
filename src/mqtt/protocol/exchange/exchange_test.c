#include "nng/exchange/exchange.h"
#include <nuts.h>

#define EX_NAME	"exchange1"
#define UNUSED(x) ((void) x)

static int match_times = 0;
static int target_times = 0;

static int match_cb(void *msg)
{
	UNUSED(msg);
	match_times++;
	return 0;
}

static int target_cb(void *msg)
{
	UNUSED(msg);
	target_times++;
	return 0;
}

void test_exchange_init(void)
{
	exchange_t *ex = NULL;

	NUTS_TRUE(exchange_init(NULL, NULL) != 0);
	NUTS_TRUE(exchange_init(&ex, NULL) != 0);

	NUTS_TRUE(exchange_init(&ex, EX_NAME) == 0);
	NUTS_TRUE(ex != NULL);
	NUTS_TRUE(ex->prods_count == 0);
	NUTS_TRUE(exchange_release(ex) == 0);

	return;
}

void test_exchange_release(void)
{
	NUTS_TRUE(exchange_release(NULL) != 0);

	return;
}

void test_exchange_producer(void)
{
	exchange_t *ex = NULL;
	NUTS_TRUE(exchange_init(&ex, EX_NAME) == 0);
	NUTS_TRUE(ex != NULL);
	NUTS_TRUE(ex->prods_count == 0);

	producer_t *p = NULL;
	NUTS_TRUE(producer_init(&p, match_cb, target_cb) == 0);
	NUTS_TRUE(p != NULL);

	NUTS_TRUE(exchange_add_prod(NULL, p) != 0);
	NUTS_TRUE(exchange_add_prod(ex, NULL) != 0);
	NUTS_TRUE(exchange_add_prod(ex, p) == 0);

	int *msg = NULL;
	msg = nng_alloc(sizeof(int));
	NUTS_TRUE(msg != NULL);

	NUTS_TRUE(exchange_handle_msg(ex, msg) == 0);
	NUTS_TRUE(match_times == 1);
	NUTS_TRUE(target_times == 1);

	nng_free(msg, sizeof(*msg));

	NUTS_TRUE(exchange_release(ex) == 0);
}

NUTS_TESTS = {
	{ "Exchange init test", test_exchange_init },
	{ "Exchange release test", test_exchange_release },
	{ "Exchange producer test", test_exchange_producer },
	{ NULL, NULL },
};
