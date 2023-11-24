#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/exchange/exchange_client.h"
#include "core/defs.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

static int match_times = 0;
static int target_times = 0;

static int
match_cb(void *msg)
{
	UNUSED(msg);
	match_times++;
	return 0;
}

static int
target_cb(void *msg)
{
	UNUSED(msg);
	target_times++;
	return 0;
}

void
test_exchange_client(void)
{
	nng_socket sock;
	exchange_t *ex;
	producer_t *p;

	NUTS_TRUE(nng_exchange_client_open(&sock) == 0);

	NUTS_TRUE(exchange_init(&ex, "exchange1") == 0);
	NUTS_TRUE(ex != NULL);


	NUTS_TRUE(producer_init(&p, match_cb, target_cb) == 0);
	NUTS_TRUE(p != NULL);
	NUTS_TRUE(exchange_add_prod(ex, p) == 0);

	nng_socket_set_ptr(sock, NNG_OPT_EXCHANGE_ADD, ex);

	int msg = 1;
	nng_sendmsg(sock, (void *)&msg, NNG_FLAG_NONBLOCK);
	nng_msleep(200);
	NUTS_TRUE(match_times == 1);
	NUTS_TRUE(target_times == 1);

	return;
}

NUTS_TESTS = {
	{ "Exchange client test", test_exchange_client },
	{ NULL, NULL },
};
