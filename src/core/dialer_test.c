//
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <string.h>

#include <nng/nng.h>

#include "nuts.h"

void
test_dialer_create(void)
{
	nng_socket  s;
	nng_dialer  d;
	const char *addr = "inproc://dialer_create";

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, addr));
	NUTS_TRUE(nng_dialer_id(d) > 0);
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_create_invalid_url(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_FAIL(nng_dialer_create(&d, s, "bogus://invalid"), NNG_ENOTSUP);
	NUTS_PASS(nng_close(s));
}

void
test_dialer_create_malformed_url(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_FAIL(nng_dialer_create(&d, s, "://invalid"), NNG_EINVAL);
	NUTS_PASS(nng_close(s));
}

void
test_dialer_close_twice(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));
	NUTS_PASS(nng_dialer_close(d));
	NUTS_FAIL(nng_dialer_close(d), NNG_ECLOSED);
	NUTS_PASS(nng_close(s));
}

void
test_dialer_start_and_close(void)
{
	nng_socket  s1;
	nng_socket  s2;
	nng_dialer  d;
	const char *addr = "inproc://dialer_start";

	NUTS_PASS(nng_pair1_open(&s1));
	NUTS_PASS(nng_pair1_open(&s2));
	NUTS_PASS(nng_listen(s1, addr, NULL, 0));
	NUTS_PASS(nng_dialer_create(&d, s2, addr));
	NUTS_PASS(nng_dialer_start(d, 0));
	nng_msleep(100); // Allow connection to establish
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s1));
	NUTS_PASS(nng_close(s2));
}

void
test_dialer_start_twice(void)
{
	nng_socket  s1;
	nng_socket  s2;
	nng_dialer  d;
	const char *addr = "inproc://dialer_start_twice";

	NUTS_PASS(nng_pair1_open(&s1));
	NUTS_PASS(nng_pair1_open(&s2));
	NUTS_PASS(nng_listen(s1, addr, NULL, 0));
	NUTS_PASS(nng_dialer_create(&d, s2, addr));
	NUTS_PASS(nng_dialer_start(d, 0));
	NUTS_FAIL(nng_dialer_start(d, 0), NNG_EBUSY);
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s1));
	NUTS_PASS(nng_close(s2));
}

void
test_dialer_id(void)
{
	nng_socket s;
	nng_dialer d1;
	nng_dialer d2;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d1, s, "inproc://test1"));
	NUTS_PASS(nng_dialer_create(&d2, s, "inproc://test2"));
	NUTS_TRUE(nng_dialer_id(d1) > 0);
	NUTS_TRUE(nng_dialer_id(d2) > 0);
	NUTS_TRUE(nng_dialer_id(d1) != nng_dialer_id(d2));
	NUTS_PASS(nng_dialer_close(d1));
	NUTS_PASS(nng_dialer_close(d2));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_id_closed(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));
	NUTS_PASS(nng_dialer_close(d));
	NUTS_TRUE(nng_dialer_id(d) < 0);
	NUTS_PASS(nng_close(s));
}

void
test_dialer_set_get_options(void)
{
	nng_socket   s;
	nng_dialer   d;
	bool         b;
	nng_duration ms;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));

	// Test duration option
	NUTS_PASS(nng_dialer_set_ms(d, NNG_OPT_RECONNMINT, 100));
	NUTS_PASS(nng_dialer_get_ms(d, NNG_OPT_RECONNMINT, &ms));
	NUTS_TRUE(ms == 100);

	// Test bool option
	NUTS_PASS(nng_dialer_set_bool(d, NNG_OPT_RECONNECT, false));
	NUTS_PASS(nng_dialer_get_bool(d, NNG_OPT_RECONNECT, &b));
	NUTS_TRUE(b == false);

	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_set_option_invalid(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));
	NUTS_FAIL(
	    nng_dialer_set_ms(d, "invalid_option", 100), NNG_ENOTSUP);
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_get_option_invalid(void)
{
	nng_socket   s;
	nng_dialer   d;
	nng_duration ms;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));
	NUTS_FAIL(
	    nng_dialer_get_ms(d, "invalid_option", &ms), NNG_ENOTSUP);
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_reconnect_time(void)
{
	nng_socket   s;
	nng_dialer   d;
	nng_duration mintime;
	nng_duration maxtime;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));

	// Set reconnect times
	NUTS_PASS(nng_dialer_set_ms(d, NNG_OPT_RECONNMINT, 50));
	NUTS_PASS(nng_dialer_set_ms(d, NNG_OPT_RECONNMAXT, 500));

	// Verify they were set
	NUTS_PASS(nng_dialer_get_ms(d, NNG_OPT_RECONNMINT, &mintime));
	NUTS_PASS(nng_dialer_get_ms(d, NNG_OPT_RECONNMAXT, &maxtime));
	NUTS_TRUE(mintime == 50);
	NUTS_TRUE(maxtime == 500);

	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_url(void)
{
	nng_socket s;
	nng_dialer d;
	char *     url;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://testurl"));
	NUTS_PASS(nng_dialer_get_string(d, NNG_OPT_URL, &url));
	NUTS_MATCH(url, "inproc://testurl");
	nng_strfree(url);
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_nonblock_flag(void)
{
	nng_socket  s1;
	nng_socket  s2;
	nng_dialer  d;
	const char *addr = "inproc://nonblock";

	NUTS_PASS(nng_pair1_open(&s1));
	NUTS_PASS(nng_pair1_open(&s2));
	NUTS_PASS(nng_listen(s1, addr, NULL, 0));
	NUTS_PASS(nng_dialer_create(&d, s2, addr));
	// Start with NNG_FLAG_NONBLOCK should return immediately
	NUTS_PASS(nng_dialer_start(d, NNG_FLAG_NONBLOCK));
	nng_msleep(50); // Give time for async connection
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s1));
	NUTS_PASS(nng_close(s2));
}

void
test_dialer_no_listener(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://nolistener"));
	NUTS_PASS(nng_dialer_set_ms(d, NNG_OPT_RECONNMINT, 10));
	NUTS_PASS(nng_dialer_set_ms(d, NNG_OPT_RECONNMAXT, 10));
	NUTS_PASS(nng_dialer_set_bool(d, NNG_OPT_RECONNECT, false));
	// Should fail to connect since no listener
	NUTS_FAIL(nng_dialer_start(d, 0), NNG_ECONNREFUSED);
	NUTS_PASS(nng_dialer_close(d));
	NUTS_PASS(nng_close(s));
}

void
test_dialer_after_socket_close(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));
	NUTS_PASS(nng_close(s));
	// Dialer should be closed when socket closes
	NUTS_FAIL(nng_dialer_start(d, 0), NNG_ECLOSED);
	NUTS_FAIL(nng_dialer_close(d), NNG_ECLOSED);
}

void
test_dialer_set_after_close(void)
{
	nng_socket s;
	nng_dialer d;

	NUTS_PASS(nng_pair1_open(&s));
	NUTS_PASS(nng_dialer_create(&d, s, "inproc://test"));
	NUTS_PASS(nng_dialer_close(d));
	NUTS_FAIL(
	    nng_dialer_set_ms(d, NNG_OPT_RECONNMINT, 100), NNG_ECLOSED);
	NUTS_PASS(nng_close(s));
}

NUTS_TESTS = {
	{ "dialer create", test_dialer_create },
	{ "dialer create invalid url", test_dialer_create_invalid_url },
	{ "dialer create malformed url", test_dialer_create_malformed_url },
	{ "dialer close twice", test_dialer_close_twice },
	{ "dialer start and close", test_dialer_start_and_close },
	{ "dialer start twice", test_dialer_start_twice },
	{ "dialer id", test_dialer_id },
	{ "dialer id closed", test_dialer_id_closed },
	{ "dialer set get options", test_dialer_set_get_options },
	{ "dialer set invalid option", test_dialer_set_option_invalid },
	{ "dialer get invalid option", test_dialer_get_option_invalid },
	{ "dialer reconnect time", test_dialer_reconnect_time },
	{ "dialer url", test_dialer_url },
	{ "dialer nonblock flag", test_dialer_nonblock_flag },
	{ "dialer no listener", test_dialer_no_listener },
	{ "dialer after socket close", test_dialer_after_socket_close },
	{ "dialer set after close", test_dialer_set_after_close },
	{ NULL, NULL },
};