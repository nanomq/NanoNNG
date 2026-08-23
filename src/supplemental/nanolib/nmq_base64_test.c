#include <string.h>

#include "nng/supplemental/nanolib/nmq_base64.h"

#include <acutest.h>

void
test_decode_strict(void)
{
	static const char *invalid[] = {
		"Zm9v!", "Zm9v\n!", "Zm=9v", "Zm9v=", "Zm9v====", "AB==", NULL
	};
	char buf[1024];

	TEST_CHECK(nmq_base64_decode_strict("Zm9v", 4, (uint8_t *) buf,
	                   sizeof(buf)) == 3);
	TEST_CHECK(nmq_base64_decode_strict("Zg==", 4, (uint8_t *) buf,
	                   sizeof(buf)) == 1);
	for (size_t i = 0; invalid[i] != NULL; i++) {
		TEST_CHECK(nmq_base64_decode_strict(
		               invalid[i], strlen(invalid[i]), (uint8_t *) buf, sizeof(buf)) ==
		    (size_t) -1);
	}
	TEST_CHECK(nmq_base64_decode_strict(
	               "Zm9v", 4, (uint8_t *) buf, 2) == (size_t) -1);
}

void
test_output_size(void)
{
	size_t input_len = 4;

	TEST_CHECK(BASE64_ENCODE_OUT_SIZE(0) == 1);
	TEST_CHECK(BASE64_ENCODE_OUT_SIZE(3) == 5);
	TEST_CHECK(BASE64_ENCODE_OUT_SIZE(SIZE_MAX - 3) == 0);
	TEST_CHECK(BASE64_ENCODE_OUT_SIZE(SIZE_MAX) == 0);
	TEST_CHECK(BASE64_DECODE_OUT_SIZE(input_len++) == 3);
	TEST_CHECK(input_len == 5);
	TEST_CHECK(BASE64_DECODE_OUT_SIZE(SIZE_MAX) == 0);
}

TEST_LIST = {
	{ "decode_strict", test_decode_strict },
	{ "output_size", test_output_size },
	{ NULL, NULL },
};
