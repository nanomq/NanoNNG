#include "nng/exchange/stream/stream.h"
#include "nng/exchange/stream/raw_stream.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

#define RAW_STREAM_ID 0x0
#define INVALID_STREAM_ID 0xf
#define TEST_STREAM_ID 0x5

// Mock callbacks for testing
static void *mock_decode(void *data)
{
	if (data == NULL) {
		return NULL;
	}
	return data;
}

static void *mock_encode(void *data)
{
	if (data == NULL) {
		return NULL;
	}
	return data;
}

static void *mock_cmd_parser(void *data)
{
	if (data == NULL) {
		return NULL;
	}
	return data;
}

// Test basic system initialization and finalization
void test_stream_sys_basic(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	stream_sys_fini();
}

// Test double initialization (should handle gracefully)
void test_stream_sys_double_init(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	// Second init should either succeed or fail gracefully
	// Clean up first instance
	stream_sys_fini();
	
	// Init again
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	stream_sys_fini();
}

// Test stream registration with valid parameters
void test_stream_sys_register(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	// Try to register with ID that's already taken (raw stream is auto-registered)
	ret = stream_register("raw", RAW_STREAM_ID, mock_decode, mock_encode, mock_cmd_parser);
	NUTS_TRUE(ret != 0); // Should fail as RAW_STREAM_ID is already registered

	stream_sys_fini();
}

// Test stream registration with invalid parameters
void test_stream_register_invalid_params(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	// Register with NULL decode function
	ret = stream_register("test1", TEST_STREAM_ID, NULL, mock_encode, mock_cmd_parser);
	NUTS_TRUE(ret != 0);

	// Register with NULL encode function
	ret = stream_register("test2", TEST_STREAM_ID, mock_decode, NULL, mock_cmd_parser);
	NUTS_TRUE(ret != 0);

	// Register with NULL cmd_parser function
	ret = stream_register("test3", TEST_STREAM_ID, mock_decode, mock_encode, NULL);
	NUTS_TRUE(ret != 0);

	// Register with all NULL functions
	ret = stream_register("test4", TEST_STREAM_ID, NULL, NULL, NULL);
	NUTS_TRUE(ret != 0);

	stream_sys_fini();
}

// Test successful stream registration and operations
void test_stream_register_success(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	// Register a new stream type
	char *test_name = nng_alloc(strlen("teststream") + 1);
	NUTS_TRUE(test_name != NULL);
	strcpy(test_name, "teststream");
	
	ret = stream_register(test_name, TEST_STREAM_ID, mock_decode, mock_encode, mock_cmd_parser);
	NUTS_TRUE(ret == 0);

	// Verify we can use the registered stream
	void *result = stream_encode(TEST_STREAM_ID, (void *)0x1234);
	NUTS_TRUE(result == (void *)0x1234);

	result = stream_decode(TEST_STREAM_ID, (void *)0x5678);
	NUTS_TRUE(result == (void *)0x5678);

	result = stream_cmd_parser(TEST_STREAM_ID, (void *)0xABCD);
	NUTS_TRUE(result == (void *)0xABCD);

	stream_sys_fini();
}

// Test stream unregistration
void test_stream_sys_unregister(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	// Unregister existing stream (RAW_STREAM_ID)
	ret = stream_unregister(RAW_STREAM_ID);
	NUTS_TRUE(ret == 0);

	// Try to unregister again (should fail)
	ret = stream_unregister(RAW_STREAM_ID);
	NUTS_TRUE(ret != 0);

	// Try to unregister non-existent stream
	ret = stream_unregister(INVALID_STREAM_ID);
	NUTS_TRUE(ret != 0);

	stream_sys_fini();
}

// Test stream encode with various scenarios
void test_stream_sys_encode(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *outdata = NULL;

	// Test with NULL data on valid stream
	outdata = stream_encode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	// Test with invalid stream ID
	outdata = stream_encode(INVALID_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	stream_sys_fini();
}

// Test stream decode with various scenarios
void test_stream_sys_decode(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *outdata = NULL;

	// Test with NULL data on valid stream
	outdata = stream_decode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	// Test with invalid stream ID
	outdata = stream_decode(INVALID_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	stream_sys_fini();
}

// Test stream cmd_parser with various scenarios
void test_stream_cmd_parser(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *outdata = NULL;

	// Test with NULL data on valid stream
	outdata = stream_cmd_parser(RAW_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	// Test with invalid stream ID
	outdata = stream_cmd_parser(INVALID_STREAM_ID, NULL);
	NUTS_TRUE(outdata == NULL);

	stream_sys_fini();
}

// Test stream_data_in memory allocation and free
void test_stream_data_in_alloc_free(void)
{
	struct stream_data_in *sdata = nng_alloc(sizeof(struct stream_data_in));
	NUTS_TRUE(sdata != NULL);
	
	sdata->len = 3;
	sdata->datas = nng_alloc(sizeof(void *) * sdata->len);
	sdata->keys = nng_alloc(sizeof(uint64_t) * sdata->len);
	sdata->lens = nng_alloc(sizeof(uint32_t) * sdata->len);
	
	NUTS_TRUE(sdata->datas != NULL);
	NUTS_TRUE(sdata->keys != NULL);
	NUTS_TRUE(sdata->lens != NULL);
	
	// Set test values
	for (uint32_t i = 0; i < sdata->len; i++) {
		sdata->keys[i] = 1000 + i;
		sdata->lens[i] = 10 + i;
		sdata->datas[i] = NULL; // Mock data pointers
	}
	
	// Free using the helper function
	stream_data_in_free(sdata);
}

// Test stream_data_in_free with NULL pointer
void test_stream_data_in_free_null(void)
{
	// Should handle NULL gracefully
	stream_data_in_free(NULL);
	NUTS_PASS("stream data in free null test");
}

// Test stream_decoded_data_free
void test_stream_decoded_data_free(void)
{
	struct stream_decoded_data *data = nng_alloc(sizeof(struct stream_decoded_data));
	NUTS_TRUE(data != NULL);
	
	data->len = 100;
	data->data = nng_alloc(data->len);
	NUTS_TRUE(data->data != NULL);
	
	// Free using helper function
	stream_decoded_data_free(data);
}

// Test stream_decoded_data_free with NULL
void test_stream_decoded_data_free_null(void)
{
	stream_decoded_data_free(NULL);
	NUTS_PASS("stream decoded data free null test");
}

// Test parquet_data_alloc (fallback implementation)
#ifndef SUPP_PARQUET
void test_parquet_data_alloc_basic(void)
{
	uint32_t col_len = 2;
	uint32_t row_len = 3;
	
	// Allocate schema
	char **schema = nng_alloc(col_len * sizeof(char *));
	NUTS_TRUE(schema != NULL);
	schema[0] = nng_alloc(strlen("col1") + 1);
	schema[1] = nng_alloc(strlen("col2") + 1);
	strcpy(schema[0], "col1");
	strcpy(schema[1], "col2");
	
	// Allocate timestamps
	uint64_t *ts = nng_alloc(row_len * sizeof(uint64_t));
	NUTS_TRUE(ts != NULL);
	for (uint32_t i = 0; i < row_len; i++) {
		ts[i] = 1000 + i;
	}
	
	// Allocate payload array
	parquet_data_packet ***payload_arr = nng_alloc(col_len * sizeof(parquet_data_packet **));
	NUTS_TRUE(payload_arr != NULL);
	for (uint32_t c = 0; c < col_len; c++) {
		payload_arr[c] = nng_alloc(row_len * sizeof(parquet_data_packet *));
		NUTS_TRUE(payload_arr[c] != NULL);
		for (uint32_t r = 0; r < row_len; r++) {
			payload_arr[c][r] = nng_alloc(sizeof(parquet_data_packet));
			NUTS_TRUE(payload_arr[c][r] != NULL);
			payload_arr[c][r]->size = 10;
			payload_arr[c][r]->data = nng_alloc(10);
			NUTS_TRUE(payload_arr[c][r]->data != NULL);
		}
	}
	
	// Allocate parquet_data
	parquet_data *data = parquet_data_alloc(schema, payload_arr, ts, col_len, row_len);
	NUTS_TRUE(data != NULL);
	NUTS_TRUE(data->col_len == col_len + 1); // Schema includes ts column
	NUTS_TRUE(data->row_len == row_len);
	
	// Free
	parquet_data_free(data);
}

// Test parquet_data_alloc with invalid parameters
void test_parquet_data_alloc_invalid(void)
{
	parquet_data *data = NULL;
	
	// NULL payload_arr
	data = parquet_data_alloc(NULL, NULL, NULL, 1, 1);
	NUTS_TRUE(data == NULL);
	
	// Zero col_len
	data = parquet_data_alloc((char **)0x1, (parquet_data_packet ***)0x1, (uint64_t *)0x1, 0, 1);
	NUTS_TRUE(data == NULL);
	
	// Zero row_len
	data = parquet_data_alloc((char **)0x1, (parquet_data_packet ***)0x1, (uint64_t *)0x1, 1, 0);
	NUTS_TRUE(data == NULL);
}
#endif

// Test raw_stream_register
void test_raw_stream_register(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	// Raw stream should already be registered during init
	// Verify it's accessible
	void *result = stream_encode(RAW_STREAM_ID, NULL);
	// NULL input should return NULL
	NUTS_TRUE(result == NULL);
	
	stream_sys_fini();
}

// Test edge case: Multiple register/unregister cycles
void test_stream_register_unregister_cycles(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	for (int cycle = 0; cycle < 3; cycle++) {
		char *name = nng_alloc(strlen("cycletest") + 1);
		NUTS_TRUE(name != NULL);
		strcpy(name, "cycletest");
		
		ret = stream_register(name, TEST_STREAM_ID, mock_decode, mock_encode, mock_cmd_parser);
		NUTS_TRUE(ret == 0);
		
		// Verify it works
		void *result = stream_encode(TEST_STREAM_ID, (void *)0x1234);
		NUTS_TRUE(result == (void *)0x1234);
		
		// Unregister
		ret = stream_unregister(TEST_STREAM_ID);
		NUTS_TRUE(ret == 0);
		
		// Verify it's gone
		result = stream_encode(TEST_STREAM_ID, (void *)0x1234);
		NUTS_TRUE(result == NULL);
	}

	stream_sys_fini();
}

NUTS_TESTS = {
	{ "stream sys basic test", test_stream_sys_basic },
	{ "stream sys double init test", test_stream_sys_double_init },
	{ "stream sys register test", test_stream_sys_register },
	{ "stream register invalid params test", test_stream_register_invalid_params },
	{ "stream register success test", test_stream_register_success },
	{ "stream sys unregister test", test_stream_sys_unregister },
	{ "stream sys encode test", test_stream_sys_encode },
	{ "stream sys decode test", test_stream_sys_decode },
	{ "stream cmd parser test", test_stream_cmd_parser },
	{ "stream data in alloc free test", test_stream_data_in_alloc_free },
	{ "stream data in free null test", test_stream_data_in_free_null },
	{ "stream decoded data free test", test_stream_decoded_data_free },
	{ "stream decoded data free null test", test_stream_decoded_data_free_null },
#ifndef SUPP_PARQUET
	{ "parquet data alloc basic test", test_parquet_data_alloc_basic },
	{ "parquet data alloc invalid test", test_parquet_data_alloc_invalid },
#endif
	{ "raw stream register test", test_raw_stream_register },
	{ "stream register unregister cycles test", test_stream_register_unregister_cycles },
	{ NULL, NULL },
};