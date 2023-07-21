//
// Copyright 2019 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <string.h>

#include <nng/nng.h>

#include "nng/supplemental/nanolib/base64.h"

#include <acutest.h>

typedef struct {
	char *decoded;
	char *encoded;
} test_case;

static test_case cases[] = {
	{ "", "" },
	{ "f", "Zg==" },
	{ "fo", "Zm8=" },
	{ "foo", "Zm9v" },
	{ "foob", "Zm9vYg==" },
	{ "fooba", "Zm9vYmE=" },
	{ "foobar", "Zm9vYmFy" },
	{ NULL, NULL },
};

void
test_encode(void)
{
	int   i;
	void *dec;

	for (i = 0; (dec = cases[i].decoded) != NULL; i++) {
		char buf[1024];
		char name[8];
		int  rv;

		(void) snprintf(name, sizeof(name), "%d", i);
		TEST_CASE(name);
		rv = base64_encode(dec, strlen(dec), buf);
		TEST_CHECK(rv >= 0);
		TEST_CHECK(rv == (int) strlen(cases[i].encoded));
		buf[rv] = 0;
		TEST_CHECK(strcmp(buf, cases[i].encoded) == 0);
	}
}

void
test_decode(void)
{
	int   i;
	void *enc;

	for (i = 0; (enc = cases[i].encoded) != NULL; i++) {
		char   buf[1024];
		char   name[8];
		size_t sz;

		(void) snprintf(name, sizeof(name), "%d", i);
		TEST_CASE(name);

		sz = base64_decode(enc, strlen(enc), (void *) buf);
		TEST_CHECK(sz != (size_t) -1);
		TEST_CHECK(sz == strlen(cases[i].decoded));
		buf[sz] = 0;
		TEST_CHECK(strcmp(buf, cases[i].decoded) == 0);
	}
}

TEST_LIST = {
	{ "encode", test_encode },
	{ "decode", test_decode },
	{ NULL, NULL },
};
