#include "nng/exchange/stream/stream.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

#define RAW_STREAM_ID 0x0
#define INVALID_STREAM_ID 0xf

void test_stream_sys_basic(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	stream_sys_fini();
}

void test_stream_sys_register(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	ret = stream_register("raw", RAW_STREAM_ID, NULL, NULL, NULL, NULL);
	NUTS_TRUE(ret != 0);

	stream_sys_fini();
}

void test_stream_sys_unregister(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	ret = stream_unregister(RAW_STREAM_ID);
	NUTS_TRUE(ret == 0);

	ret = stream_unregister(RAW_STREAM_ID);
	NUTS_TRUE(ret != 0);

	stream_sys_fini();
}

void test_stream_sys_encode(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *outdata = NULL;

	outdata = stream_encode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	outdata = stream_encode(INVALID_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	stream_sys_fini();
}

void test_stream_sys_decode(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *outdata = NULL;

	outdata = stream_decode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	outdata = stream_decode(INVALID_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	stream_sys_fini();
}

NUTS_TESTS = {
	{ "stream sys basic test", test_stream_sys_basic },
	{ "stream sys register test", test_stream_sys_register },
	{ "stream sys unregister test", test_stream_sys_unregister },
	{ "stream sys encode test", test_stream_sys_encode },
	{ "stream sys decode test", test_stream_sys_decode },
	{ NULL, NULL },
};
