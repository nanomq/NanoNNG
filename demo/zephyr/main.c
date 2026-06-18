/*
 * NanoNNG Zephyr Demo
 *
 * Minimal PAIRv0 test over inproc transport.
 * This verifies the core library compiles and initializes correctly.
 *
 * Build: west build -b qemu_x86 .
 * Run:   west build -t run
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <nng/transport/inproc/inproc.h>

void main(void)
{
	nng_socket s1;
	nng_socket s2;
	int        rv;

	printk("\n=== NanoNNG Zephyr Demo ===\n");

	// Open PAIR sockets
	rv = nng_pair0_open(&s1);
	if (rv != 0) {
		printk("FAIL: nng_pair0_open(s1): %s\n", nng_strerror(rv));
		return;
	}
	printk("PASS: nng_pair0_open(s1)\n");

	rv = nng_pair0_open(&s2);
	if (rv != 0) {
		printk("FAIL: nng_pair0_open(s2): %s\n", nng_strerror(rv));
		nng_close(s1);
		return;
	}
	printk("PASS: nng_pair0_open(s2)\n");

	// Listen on inproc
	rv = nng_listen(s1, "inproc://test", NULL, 0);
	if (rv != 0) {
		printk("FAIL: nng_listen: %s\n", nng_strerror(rv));
		nng_close(s2);
		nng_close(s1);
		return;
	}
	printk("PASS: nng_listen(inproc://test)\n");

	// Dial
	rv = nng_dial(s2, "inproc://test", NULL, 0);
	if (rv != 0) {
		printk("FAIL: nng_dial: %s\n", nng_strerror(rv));
		nng_close(s2);
		nng_close(s1);
		return;
	}
	printk("PASS: nng_dial(inproc://test)\n");

	// Send a message
	rv = nng_send(s2, "hello", 6, 0);
	if (rv != 0) {
		printk("FAIL: nng_send: %s\n", nng_strerror(rv));
	} else {
		printk("PASS: nng_send(\"hello\")\n");
	}

	// Receive the message
	char   *buf = NULL;
	size_t  sz  = 0;
	rv = nng_recv(s1, &buf, &sz, NNG_FLAG_ALLOC);
	if (rv != 0) {
		printk("FAIL: nng_recv: %s\n", nng_strerror(rv));
	} else {
		printk("PASS: nng_recv -> \"%.*s\" (%d bytes)\n",
		    (int) sz, buf, (int) sz);
		nng_free(buf, sz);
	}

	// Cleanup
	nng_close(s2);
	nng_close(s1);
	printk("=== Demo Complete ===\n\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
