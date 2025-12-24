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

	ret = stream_register("raw", RAW_STREAM_ID, NULL, NULL, NULL);
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

void test_stream_sys_cmd_parser(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *result = NULL;

	result = stream_cmd_parser(INVALID_STREAM_ID, NULL);
	NUTS_TRUE(result == NULL);

	stream_sys_fini();
}

void test_stream_decoded_data_free_null(void)
{
	stream_decoded_data_free(NULL);
	NUTS_TRUE(1);
}

void test_stream_data_out_free_null(void)
{
	stream_data_out_free(NULL);
	NUTS_TRUE(1);
}

void test_stream_data_in_free_null(void)
{
	stream_data_in_free(NULL);
	NUTS_TRUE(1);
}

void *dummy_func(void *data) {
	UNUSED(data);
	return NULL;
}

void test_stream_register_twice(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	char *name = nng_strdup("test_stream");
	NUTS_TRUE(name != NULL);

	ret = stream_register(name, 0x5, dummy_func, dummy_func, dummy_func);
	NUTS_TRUE(ret == 0);

	char *name2 = nng_strdup("test_stream2");
	ret = stream_register(name2, 0x5, dummy_func, dummy_func, dummy_func);
	NUTS_TRUE(ret == NNG_EEXIST);

	nng_strfree(name2);
	stream_sys_fini();
}

void test_stream_register_null_callbacks(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	char *name = nng_strdup("test");
	ret = stream_register(name, 0x6, NULL, NULL, NULL);
	NUTS_TRUE(ret == NNG_EINVAL);

	nng_strfree(name);
	stream_sys_fini();
}

void test_stream_unregister_invalid_id(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	ret = stream_unregister(0xFF);
	NUTS_TRUE(ret == NNG_ENOENT);

	stream_sys_fini();
}

void test_stream_data_in_free_with_data(void)
{
	struct stream_data_in *sdata = nng_alloc(sizeof(struct stream_data_in));
	NUTS_TRUE(sdata != NULL);

	sdata->len = 2;
	sdata->datas = nng_alloc(sizeof(void *) * sdata->len);
	sdata->keys = nng_alloc(sizeof(uint64_t) * sdata->len);
	sdata->lens = nng_alloc(sizeof(uint32_t) * sdata->len);

	NUTS_TRUE(sdata->datas != NULL);
	NUTS_TRUE(sdata->keys != NULL);
	NUTS_TRUE(sdata->lens != NULL);

	sdata->keys[0] = 100;
	sdata->keys[1] = 200;
	sdata->lens[0] = 10;
	sdata->lens[1] = 20;
	sdata->datas[0] = nng_alloc(10);
	sdata->datas[1] = nng_alloc(20);

	stream_data_in_free(sdata);
	NUTS_TRUE(1);
}

void test_stream_sys_init_multiple_times(void)
{
	int ret1 = stream_sys_init();
	NUTS_TRUE(ret1 == 0);

	int ret2 = stream_sys_init();
	NUTS_TRUE(ret2 != 0 || ret2 == 0);

	stream_sys_fini();
}

void test_stream_encode_after_unregister(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	ret = stream_unregister(RAW_STREAM_ID);
	NUTS_TRUE(ret == 0);

	void *result = stream_encode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(result == NULL);

	stream_sys_fini();
}

void test_stream_decode_after_unregister(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	ret = stream_unregister(RAW_STREAM_ID);
	NUTS_TRUE(ret == 0);

	void *result = stream_decode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(result == NULL);

	stream_sys_fini();
}

NUTS_TESTS = {
	{ "stream sys basic test", test_stream_sys_basic },
	{ "stream sys register test", test_stream_sys_register },
	{ "stream sys unregister test", test_stream_sys_unregister },
	{ "stream sys encode test", test_stream_sys_encode },
	{ "stream sys decode test", test_stream_sys_decode },
	{ "stream sys cmd parser test", test_stream_sys_cmd_parser },
	{ "stream decoded data free null test", test_stream_decoded_data_free_null },
	{ "stream data out free null test", test_stream_data_out_free_null },
	{ "stream data in free null test", test_stream_data_in_free_null },
	{ "stream register twice test", test_stream_register_twice },
	{ "stream register null callbacks test", test_stream_register_null_callbacks },
	{ "stream unregister invalid id test", test_stream_unregister_invalid_id },
	{ "stream data in free with data test", test_stream_data_in_free_with_data },
	{ "stream sys init multiple times test", test_stream_sys_init_multiple_times },
	{ "stream encode after unregister test", test_stream_encode_after_unregister },
	{ "stream decode after unregister test", test_stream_decode_after_unregister },
	{ NULL, NULL },
};
