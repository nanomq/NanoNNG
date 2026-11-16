// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT

#include <nuts.h>
#include "nng/supplemental/nanolib/md5.h"

static void
check_digest(unsigned char *data, unsigned int len, const char *expected)
{
	MD5_CTX ctx;
	unsigned char digest[16];
	char hex[33];
	MD5Init(&ctx);
	MD5Update(&ctx, data, len);
	MD5Final(&ctx, digest);
	for (unsigned int i = 0; i < sizeof(digest); i++) {
		snprintf(hex + 2 * i, 3, "%02x", digest[i]);
	}
	NUTS_TRUE(strcmp(hex, expected) == 0);
}

static void
test_md5_binary_and_text(void)
{
	unsigned char binary[256];
	for (unsigned int i = 0; i < sizeof(binary); i++) {
		binary[i] = (unsigned char) i;
	}
	check_digest((unsigned char *) "", 0,
	    "d41d8cd98f00b204e9800998ecf8427e");
	check_digest((unsigned char *) "abc", 3,
	    "900150983cd24fb0d6963f7d28e17f72");
	/* High-bit bytes exercise unsigned 24-bit loads in the block decoder. */
	check_digest(binary, sizeof(binary),
	    "e2c865db4162bed963bfaa9ef6ac18f0");
}

static void
test_md5_streamed_binary(void)
{
	unsigned char binary[256];
	const unsigned int chunks[] = { 1, 7, 63, 64, 65 };
	for (unsigned int i = 0; i < sizeof(binary); i++) {
		binary[i] = (unsigned char) i;
	}
	for (unsigned int c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
		MD5_CTX ctx;
		unsigned char digest[16];
		char hex[33];
		MD5Init(&ctx);
		for (unsigned int offset = 0; offset < sizeof(binary);) {
			unsigned int n = chunks[c];
			if (n > sizeof(binary) - offset) n = sizeof(binary) - offset;
			MD5Update(&ctx, binary + offset, n);
			offset += n;
		}
		MD5Final(&ctx, digest);
		for (unsigned int i = 0; i < sizeof(digest); i++) {
			snprintf(hex + 2 * i, 3, "%02x", digest[i]);
		}
		NUTS_TRUE(strcmp(hex, "e2c865db4162bed963bfaa9ef6ac18f0") == 0);
	}
}

NUTS_TESTS = {
	{ "MD5 text and high-bit binary vectors", test_md5_binary_and_text },
	{ "MD5 high-bit binary across update boundaries", test_md5_streamed_binary },
	{ NULL, NULL },
};
