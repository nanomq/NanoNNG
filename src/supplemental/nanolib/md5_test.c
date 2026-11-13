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

NUTS_TESTS = {
	{ "MD5 text and high-bit binary vectors", test_md5_binary_and_text },
	{ NULL, NULL },
};
