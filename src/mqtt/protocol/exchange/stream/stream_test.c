// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include "nng/exchange/stream/stream.h"
#include "nng/exchange/stream/raw_stream.h"
#include "nng/nng.h"
#include <nuts.h>
#include <string.h>

#define UNUSED(x) ((void) x)

#define RAW_STREAM_ID 0x0
#define INVALID_STREAM_ID 0xf
#define TEST_STREAM_ID 0x10

static nng_msg *
test_make_payload_msg(const void *data, size_t len)
{
	nng_msg *m = NULL;

	if (nng_msg_alloc(&m, len) != 0) {
		return NULL;
	}
	if (len > 0 && data != NULL) {
		memcpy(nng_msg_body(m), data, len);
	}
	nng_msg_set_payload_ptr(m, nng_msg_body(m));
	return m;
}

static void
test_free_input_msgs(struct stream_data_in *input)
{
	if (input == NULL) {
		return;
	}
	if (input->msgs != NULL) {
		for (uint32_t i = 0; i < input->len; i++) {
			if (input->msgs[i] != NULL) {
				nng_msg_free(input->msgs[i]);
			}
		}
		nng_free(input->msgs, sizeof(void *) * input->len);
	}
	if (input->keys != NULL) {
		nng_free(input->keys, sizeof(uint64_t) * input->len);
	}
}

// ============================================================================
// Basic System Tests
// ============================================================================

void test_stream_sys_basic(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	stream_sys_fini();
}

void test_stream_sys_double_init(void)
{
	int ret = 0;
	
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	stream_sys_fini();
}

void test_stream_sys_double_fini(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	stream_sys_fini();
	stream_sys_fini();
	NUTS_PASS(0);
}

// ============================================================================
// Registration Tests
// ============================================================================

void test_stream_sys_register(void)
{
	int ret = 0;
	ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	ret = stream_register("raw", RAW_STREAM_ID, NULL, NULL, NULL);
	NUTS_TRUE(ret != 0);

	stream_sys_fini();
}

void test_stream_register_null_callbacks(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	ret = stream_register("test", TEST_STREAM_ID, NULL, NULL, NULL);
	NUTS_TRUE(ret == NNG_EINVAL);
	
	stream_sys_fini();
}

void test_stream_register_duplicate_id(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	void *dummy_fn = (void*)0x1;
	ret = stream_register("duplicate", RAW_STREAM_ID, dummy_fn, dummy_fn, dummy_fn);
	NUTS_TRUE(ret == NNG_EEXIST);
	
	stream_sys_fini();
}

// ============================================================================
// Unregister Tests
// ============================================================================

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

void test_stream_unregister_invalid_id(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	ret = stream_unregister(INVALID_STREAM_ID);
	NUTS_TRUE(ret == NNG_ENOENT);
	
	stream_sys_fini();
}

// ============================================================================
// Encode Tests
// ============================================================================

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

void test_stream_encode_with_valid_data(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	input.len = 2;
	input.keys = nng_alloc(sizeof(uint64_t) * 2);
	input.msgs = nng_alloc(sizeof(void*) * 2);
	
	NUTS_ASSERT(input.keys != NULL);
	NUTS_ASSERT(input.msgs != NULL);
	
	input.keys[0] = 1000;
	input.keys[1] = 2000;
	input.msgs[0] = test_make_payload_msg("test", 4);
	input.msgs[1] = test_make_payload_msg("testdata", 8);
	NUTS_ASSERT(input.msgs[0] != NULL);
	NUTS_ASSERT(input.msgs[1] != NULL);
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	test_free_input_msgs(&input);
	
	stream_sys_fini();
}

void test_stream_encode_empty_data(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	input.len = 0;
	input.keys = NULL;
	input.msgs = NULL;
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	NUTS_TRUE(output == NULL);
	
	stream_sys_fini();
}

void test_stream_encode_single_element(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	input.len = 1;
	input.keys = nng_alloc(sizeof(uint64_t));
	input.msgs = nng_alloc(sizeof(void*));
	
	input.keys[0] = 12345;
	input.msgs[0] = test_make_payload_msg("hello", 5);
	NUTS_ASSERT(input.msgs[0] != NULL);
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	test_free_input_msgs(&input);
	
	stream_sys_fini();
}

void test_stream_encode_large_payload(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	size_t large_size = 1024; // 1KB
	uint8_t *buf;
	
	input.len = 1;
	input.keys = nng_alloc(sizeof(uint64_t));
	input.msgs = nng_alloc(sizeof(void*));
	
	input.keys[0] = 99999;
	buf = nng_alloc(large_size);
	NUTS_ASSERT(buf != NULL);
	memset(buf, 0xAA, large_size);
	input.msgs[0] = test_make_payload_msg(buf, large_size);
	nng_free(buf, large_size);
	NUTS_ASSERT(input.msgs[0] != NULL);
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	test_free_input_msgs(&input);
	
	stream_sys_fini();
}

void test_stream_encode_zero_length_payload(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	input.len = 1;
	input.keys = nng_alloc(sizeof(uint64_t));
	input.msgs = nng_alloc(sizeof(void*));
	
	input.keys[0] = 100;
	input.msgs[0] = test_make_payload_msg(NULL, 0);
	NUTS_ASSERT(input.msgs[0] != NULL);
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	test_free_input_msgs(&input);
	
	stream_sys_fini();
}

// ============================================================================
// Decode Tests
// ============================================================================

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

// ============================================================================
// Command Parser Tests
// ============================================================================

void test_stream_cmd_parser_null(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	void *result = stream_cmd_parser(RAW_STREAM_ID, NULL);
	NUTS_TRUE(result == NULL);
	
	stream_sys_fini();
}

void test_stream_cmd_parser_invalid_id(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	void *result = stream_cmd_parser(INVALID_STREAM_ID, NULL);
	NUTS_TRUE(result == NULL);
	
	stream_sys_fini();
}

void test_stream_cmd_parser_sync_format(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	const char *cmd = "sync-1000-2000";
	struct cmd_data *result = (struct cmd_data*)stream_cmd_parser(RAW_STREAM_ID, (void*)cmd);
	
	if (result != NULL) {
		NUTS_TRUE(result->is_sync == true);
		NUTS_TRUE(result->start_key == 1000);
		NUTS_TRUE(result->end_key == 2000);
		
		if (result->schema != NULL) {
			for (uint32_t i = 0; i < result->schema_len; i++) {
				if (result->schema[i] != NULL) {
					nng_free(result->schema[i], strlen(result->schema[i]) + 1);
				}
			}
			nng_free(result->schema, sizeof(char*) * result->schema_len);
		}
		nng_free(result, sizeof(struct cmd_data));
	}
	
	stream_sys_fini();
}

void test_stream_cmd_parser_async_format(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	const char *cmd = "async-5000-10000";
	struct cmd_data *result = (struct cmd_data*)stream_cmd_parser(RAW_STREAM_ID, (void*)cmd);
	
	if (result != NULL) {
		NUTS_TRUE(result->is_sync == false);
		NUTS_TRUE(result->start_key == 5000);
		NUTS_TRUE(result->end_key == 10000);
		
		if (result->schema != NULL) {
			for (uint32_t i = 0; i < result->schema_len; i++) {
				if (result->schema[i] != NULL) {
					nng_free(result->schema[i], strlen(result->schema[i]) + 1);
				}
			}
			nng_free(result->schema, sizeof(char*) * result->schema_len);
		}
		nng_free(result, sizeof(struct cmd_data));
	}
	
	stream_sys_fini();
}

void test_stream_cmd_parser_invalid_format(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	const char *invalid_cmds[] = {
		"invalid",
		"sync-abc-def",
		"async",
		"sync-1000",
		"1000-2000",
	};
	
	for (size_t i = 0; i < sizeof(invalid_cmds)/sizeof(invalid_cmds[0]); i++) {
		void *result = stream_cmd_parser(RAW_STREAM_ID, (void*)invalid_cmds[i]);
		NUTS_TRUE(result == NULL);
	}
	
	stream_sys_fini();
}

// ============================================================================
// Memory Management Tests
// ============================================================================

void test_stream_decoded_data_free_null(void)
{
	stream_decoded_data_free(NULL);
	NUTS_PASS(0);
}

void test_stream_data_out_free_null(void)
{
	stream_data_out_free(NULL);
	NUTS_PASS(0);
}

void test_stream_data_in_free_null(void)
{
	stream_data_in_free(NULL);
	NUTS_PASS(0);
}

void test_parquet_data_free_null(void)
{
	parquet_data_free(NULL);
	NUTS_PASS(0);
}

// ============================================================================
// Integration Tests
// ============================================================================

void test_raw_stream_register_unregister(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	ret = stream_unregister(RAW_STREAM_ID);
	NUTS_TRUE(ret == 0);
	
	ret = raw_stream_register();
	NUTS_TRUE(ret == 0);
	
	ret = raw_stream_register();
	NUTS_TRUE(ret == NNG_EEXIST);
	
	stream_sys_fini();
}

void test_stream_multiple_elements(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	uint32_t count = 10;
	struct stream_data_in input;
	
	input.len = count;
	input.keys = nng_alloc(sizeof(uint64_t) * count);
	input.msgs = nng_alloc(sizeof(void*) * count);
	
	for (uint32_t i = 0; i < count; i++) {
		uint8_t buf[16];
		uint32_t plen = (i % 10) + 1;
		input.keys[i] = i * 1000;
		memset(buf, i & 0xFF, plen);
		input.msgs[i] = test_make_payload_msg(buf, plen);
		NUTS_ASSERT(input.msgs[i] != NULL);
	}
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	test_free_input_msgs(&input);
	
	stream_sys_fini();
}

// ============================================================================
// Test List
// ============================================================================

NUTS_TESTS = {
	{ "stream sys basic test", test_stream_sys_basic },
	{ "stream sys double init", test_stream_sys_double_init },
	{ "stream sys double fini", test_stream_sys_double_fini },
	{ "stream sys register test", test_stream_sys_register },
	{ "stream register null callbacks", test_stream_register_null_callbacks },
	{ "stream register duplicate id", test_stream_register_duplicate_id },
	{ "stream sys unregister test", test_stream_sys_unregister },
	{ "stream unregister invalid id", test_stream_unregister_invalid_id },
	{ "stream sys encode test", test_stream_sys_encode },
	{ "stream encode with valid data", test_stream_encode_with_valid_data },
	{ "stream encode empty data", test_stream_encode_empty_data },
	{ "stream encode single element", test_stream_encode_single_element },
	{ "stream encode large payload", test_stream_encode_large_payload },
	{ "stream encode zero length payload", test_stream_encode_zero_length_payload },
	{ "stream sys decode test", test_stream_sys_decode },
	{ "stream cmd parser null", test_stream_cmd_parser_null },
	{ "stream cmd parser invalid id", test_stream_cmd_parser_invalid_id },
	{ "stream cmd parser sync format", test_stream_cmd_parser_sync_format },
	{ "stream cmd parser async format", test_stream_cmd_parser_async_format },
	{ "stream cmd parser invalid format", test_stream_cmd_parser_invalid_format },
	{ "stream decoded data free null", test_stream_decoded_data_free_null },
	{ "stream data out free null", test_stream_data_out_free_null },
	{ "stream data in free null", test_stream_data_in_free_null },
	{ "parquet data free null", test_parquet_data_free_null },
	{ "raw stream register unregister", test_raw_stream_register_unregister },
	{ "stream multiple elements", test_stream_multiple_elements },
	{ NULL, NULL },
};
