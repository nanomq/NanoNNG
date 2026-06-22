#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <nng/transport/inproc/inproc.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void test_pass(const char *msg) { g_pass++; printk("  PASS: %s\n", msg); }
static void test_fail(const char *msg, int rv) { g_fail++; printk("  FAIL: %s: %s\n", msg, nng_strerror(rv)); }

void main(void)
{
	int failed = 0;
	printk("\n========================================\n");
	printk("  NanoNNG Zephyr - Tests (mps2_an385)\n");
	printk("========================================\n");

	// Test 1: Basic Inproc
	{
		nng_socket s1, s2; char *buf = NULL; size_t sz = 0; int rv;
		printk("\n--- Test 1: Basic Inproc ---\n");
		rv = nng_pair0_open(&s1);
		if (rv) { test_fail("open s1", rv); goto t1done; } else test_pass("open s1");
		rv = nng_pair0_open(&s2);
		if (rv) { test_fail("open s2", rv); nng_close(s1); goto t1done; } else test_pass("open s2");
		rv = nng_listen(s1, "inproc://t1", NULL, 0);
		if (rv) { test_fail("listen", rv); nng_close(s2); nng_close(s1); goto t1done; } else test_pass("listen");
		rv = nng_dial(s2, "inproc://t1", NULL, 0);
		if (rv) { test_fail("dial", rv); nng_close(s2); nng_close(s1); goto t1done; } else test_pass("dial");
		rv = nng_send(s2, "hello", 6, 0);
		if (rv) { test_fail("send", rv); nng_close(s2); nng_close(s1); goto t1done; } else test_pass("send");
		rv = nng_recv(s1, &buf, &sz, NNG_FLAG_ALLOC);
		if (rv) { test_fail("recv", rv); nng_close(s2); nng_close(s1); goto t1done; } else if (sz == 6 && !memcmp(buf, "hello", 6)) test_pass("recv");
		else test_fail("recv mismatch", -1);
		nng_free(buf, sz);
		nng_close(s2); nng_close(s1);
		t1done: if (rv) failed++;
	}

	// Test 2: Socket Lifecycle (3 cycles)
	{
		printk("\n--- Test 2: Socket Lifecycle (3) ---\n");
		for (int i = 0; i < 3; i++) { nng_socket s; int rv = nng_pair0_open(&s); if (rv) { test_fail("open", rv); failed++; goto t2done; } rv = nng_close(s); if (rv) { test_fail("close", rv); failed++; goto t2done; } printk("  cycle %d OK\n", i); }
		test_pass("lifecycle"); t2done:;
	}

	// Test 3: Message Stress (3 msgs)
	{
		nng_socket s1, s2; int rv;
		printk("\n--- Test 3: Message Stress (3) ---\n");
		rv = nng_pair0_open(&s1);
		if (rv) { test_fail("open s1", rv); failed++; goto t3done; }
		rv = nng_pair0_open(&s2);
		if (rv) { test_fail("open s2", rv); nng_close(s1); failed++; goto t3done; }
		rv = nng_listen(s1, "inproc://t3", NULL, 0);
		if (rv) { test_fail("listen", rv); nng_close(s2); nng_close(s1); failed++; goto t3done; }
		rv = nng_dial(s2, "inproc://t3", NULL, 0);
		if (rv) { test_fail("dial", rv); nng_close(s2); nng_close(s1); failed++; goto t3done; }
		for (int i = 0; i < 3; i++) {
			char exp[16]; snprintf(exp, sizeof(exp), "m%d", i);
			rv = nng_send(s2, exp, strlen(exp)+1, 0);
			if (rv) { test_fail("send", rv); nng_close(s2); nng_close(s1); failed++; goto t3done; }
			char *buf = NULL; size_t sz = 0;
			rv = nng_recv(s1, &buf, &sz, NNG_FLAG_ALLOC);
			if (rv) { test_fail("recv", rv); nng_close(s2); nng_close(s1); failed++; goto t3done; }
			nng_free(buf, sz);
			printk("  msg %d OK\n", i);
		}
		test_pass("message stress"); nng_close(s2); nng_close(s1);
		t3done:;
	}

	// Test 4: Batch Pairs
	{
		printk("\n--- Test 4: Batch Pairs ---\n");
		for (int r = 0; r < 2; r++) {
			nng_socket s1, s2; int rv; char url[32];
			snprintf(url, sizeof(url), "inproc://b%d", r);
			rv = nng_pair0_open(&s1);
			if (rv) { test_fail("open s1", rv); failed++; goto t4done; }
			rv = nng_pair0_open(&s2);
			if (rv) { test_fail("open s2", rv); nng_close(s1); failed++; goto t4done; }
			rv = nng_listen(s1, url, NULL, 0);
			if (rv) { test_fail("listen", rv); nng_close(s2); nng_close(s1); failed++; goto t4done; }
			rv = nng_dial(s2, url, NULL, 0);
			if (rv) { test_fail("dial", rv); nng_close(s2); nng_close(s1); failed++; goto t4done; }
			rv = nng_send(s2, "ping", 5, 0);
			if (rv) { test_fail("send", rv); nng_close(s2); nng_close(s1); failed++; goto t4done; }
			char *buf = NULL; size_t sz = 0;
			rv = nng_recv(s1, &buf, &sz, NNG_FLAG_ALLOC);
			if (rv) { test_fail("recv", rv); nng_close(s2); nng_close(s1); failed++; goto t4done; }
			nng_free(buf, sz);
			nng_close(s2); nng_close(s1);
			k_sleep(K_MSEC(20));
			printk("  round %d OK\n", r);
		}
		test_pass("batch pairs"); t4done:;
	}

	printk("\n========================================\n");
	printk("  Results: %d PASS, %d FAIL, %d TESTS FAILED\n", g_pass, g_fail, failed);
	printk("========================================\n\n");
	while (1) { k_sleep(K_SECONDS(1)); }
}
