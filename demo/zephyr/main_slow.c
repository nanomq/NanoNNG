#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <nng/transport/inproc/inproc.h>

#include <string.h>

#define SOCKET_CYCLE_COUNT 10
#define MSG_STRESS_COUNT   20

static int g_pass = 0;
static int g_fail = 0;

static void test_pass(const char *msg) { g_pass++; printk("  PASS: %s\n", msg); }
static void test_fail(const char *msg, int rv) { g_fail++; printk("  FAIL: %s: %s\n", msg, nng_strerror(rv)); }

static int test_basic_inproc(void)
{
	nng_socket s1, s2;
	char *buf = NULL;
	size_t sz = 0;
	int rv;

	printk("\n--- Test 1: Basic Inproc ---\n");

	rv = nng_pair0_open(&s1);
	if (rv != 0) { test_fail("nng_pair0_open(s1)", rv); return -1; }
	test_pass("nng_pair0_open(s1)");

	rv = nng_pair0_open(&s2);
	if (rv != 0) { test_fail("nng_pair0_open(s2)", rv); nng_close(s1); return -1; }
	test_pass("nng_pair0_open(s2)");

	rv = nng_listen(s1, "inproc://test_basic", NULL, 0);
	if (rv != 0) { test_fail("nng_listen", rv); nng_close(s2); nng_close(s1); return -1; }
	test_pass("nng_listen");

	rv = nng_dial(s2, "inproc://test_basic", NULL, 0);
	if (rv != 0) { test_fail("nng_dial", rv); nng_close(s2); nng_close(s1); return -1; }
	test_pass("nng_dial");

	rv = nng_send(s2, "hello", 6, 0);
	if (rv != 0) { test_fail("nng_send", rv); nng_close(s2); nng_close(s1); return -1; }
	test_pass("nng_send");

	rv = nng_recv(s1, &buf, &sz, NNG_FLAG_ALLOC);
	if (rv != 0) { test_fail("nng_recv", rv); nng_close(s2); nng_close(s1); return -1; }
	if (sz == 6 && memcmp(buf, "hello", 6) == 0)
		test_pass("nng_recv('hello', 6 bytes)");
	else
		test_fail("nng_recv content mismatch", NNG_EINVAL);
	nng_free(buf, sz);

	nng_close(s2);
	nng_close(s1);
	return 0;
}

static int test_socket_lifecycle(void)
{
	printk("\n--- Test 2: Socket Lifecycle (%d cycles) ---\n", SOCKET_CYCLE_COUNT);
	for (int i = 0; i < SOCKET_CYCLE_COUNT; i++) {
		nng_socket s; int rv;
		rv = nng_pair0_open(&s);
		if (rv != 0) { test_fail("pair0_open", rv); return -1; }
		rv = nng_close(s);
		if (rv != 0) { test_fail("close", rv); return -1; }
		if ((i+1) % 5 == 0) printk("  ... %d cycles\n", i+1);
	}
	test_pass("socket create/destroy cycle");
	return 0;
}

static int test_message_stress(void)
{
	nng_socket s1, s2;
	int rv;
	char url[] = "inproc://msg_stress";

	printk("\n--- Test 3: Message Stress (%d msgs) ---\n", MSG_STRESS_COUNT);

	rv = nng_pair0_open(&s1);
	if (rv != 0) { test_fail("pair0_open(s1)", rv); return -1; }
	rv = nng_pair0_open(&s2);
	if (rv != 0) { test_fail("pair0_open(s2)", rv); nng_close(s1); return -1; }
	rv = nng_listen(s1, url, NULL, 0);
	if (rv != 0) { test_fail("listen", rv); nng_close(s2); nng_close(s1); return -1; }
	rv = nng_dial(s2, url, NULL, 0);
	if (rv != 0) { test_fail("dial", rv); nng_close(s2); nng_close(s1); return -1; }

	for (int i = 0; i < MSG_STRESS_COUNT; i++) {
		char exp[32];
		int len = snprintf(exp, sizeof(exp), "msg-%d", i);
		rv = nng_send(s2, exp, (size_t)len + 1, 0);
		if (rv != 0) { test_fail("send", rv); goto out; }
		char *buf = NULL; size_t sz = 0;
		rv = nng_recv(s1, &buf, &sz, NNG_FLAG_ALLOC);
		if (rv != 0) { test_fail("recv", rv); goto out; }
		if (sz != (size_t)len + 1 || memcmp(buf, exp, sz) != 0) {
			test_fail("content mismatch", NNG_EINVAL);
			nng_free(buf, sz); goto out;
		}
		nng_free(buf, sz);
	}
	test_pass("message send/recv stress");
	rv = 0;
out:
	nng_close(s2); nng_close(s1);
	return (rv == 0) ? 0 : -1;
}

static int test_batch_pairs(void)
{
	printk("\n--- Test 4: Batch Pair Lifecycle ---\n");

	for (int round = 0; round < 2; round++) {
		nng_socket s1, s2;
		int rv;
		char url[32];

		snprintf(url, sizeof(url), "inproc://batch_%d", round);
		rv = nng_pair0_open(&s1);
		if (rv != 0) { test_fail("open s1", rv); return -1; }
		rv = nng_pair0_open(&s2);
		if (rv != 0) { test_fail("open s2", rv); nng_close(s1); return -1; }
		rv = nng_listen(s1, url, NULL, 0);
		if (rv != 0) { test_fail("listen", rv); nng_close(s2); nng_close(s1); return -1; }
		rv = nng_dial(s2, url, NULL, 0);
		if (rv != 0) { test_fail("dial", rv); nng_close(s2); nng_close(s1); return -1; }
		rv = nng_send(s2, "ping", 5, 0);
		if (rv != 0) { test_fail("send", rv); nng_close(s2); nng_close(s1); return -1; }
		char *buf = NULL; size_t sz = 0;
		rv = nng_recv(s1, &buf, &sz, NNG_FLAG_ALLOC);
		if (rv != 0) { test_fail("recv", rv); nng_close(s2); nng_close(s1); return -1; }
		nng_free(buf, sz);
		nng_close(s2);
		nng_close(s1);
		k_sleep(K_MSEC(20));
		printk("  round %d OK\n", round);
	}
	test_pass("batch pair lifecycle");
	return 0;
}

void main(void)
{
	int failed = 0;

	printk("\n========================================\n");
	printk("  NanoNNG Zephyr - Security Fix Tests\n");
	printk("========================================\n");

	if (test_basic_inproc() != 0) failed++;
	if (test_socket_lifecycle() != 0) failed++;
	if (test_message_stress() != 0) failed++;
	if (test_batch_pairs() != 0) failed++;

	printk("\n========================================\n");
	printk("  Results: %d PASS, %d FAIL, %d TESTS FAILED\n",
	    g_pass, g_fail, failed);
	printk("========================================\n\n");

	while (1) { k_sleep(K_SECONDS(1)); }
}
