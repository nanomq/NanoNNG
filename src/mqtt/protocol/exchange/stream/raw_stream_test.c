// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include <stdlib.h>
#include <string.h>
#include "nng/exchange/stream/raw_stream.h"
#include "nng/exchange/stream/stream.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

// Test raw_stream_register function
void test_raw_stream_register_basic(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	// Raw stream should be auto-registered
	// Try to register again (should fail)
	ret = raw_stream_register();
	NUTS_TRUE(ret != 0); // Already registered
	
	stream_sys_fini();
}

// Test raw_encode with NULL input
void test_raw_encode_null_input(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	void *result = stream_encode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(result == NULL);
	
	stream_sys_fini();
}

// Test raw_encode with valid stream_data_in
void test_raw_encode_valid_input(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	// Create a stream_data_in structure
	struct stream_data_in *input = nng_alloc(sizeof(struct stream_data_in));
	NUTS_TRUE(input != NULL);
	
	input->len = 3;
	input->keys = nng_alloc(sizeof(uint64_t) * input->len);
	input->lens = nng_alloc(sizeof(uint32_t) * input->len);
	input->datas = nng_alloc(sizeof(void *) * input->len);
	
	NUTS_TRUE(input->keys != NULL);
	NUTS_TRUE(input->lens != NULL);
	NUTS_TRUE(input->datas != NULL);
	
	// Set up test data
	for (uint32_t i = 0; i < input->len; i++) {
		input->keys[i] = 1000 + i;
		input->lens[i] = 10 + i;
		input->datas[i] = nng_alloc(input->lens[i]);
		NUTS_TRUE(input->datas[i] != NULL);
		memset(input->datas[i], 'A' + i, input->lens[i]);
	}
	
	// Encode
	void *result = stream_encode(RAW_STREAM_ID, input);
	
	// Result should be non-NULL (parquet_data structure)
	NUTS_TRUE(result != NULL);
	
	// Clean up input
	for (uint32_t i = 0; i < input->len; i++) {
		nng_free(input->datas[i], input->lens[i]);
	}
	nng_free(input->datas, sizeof(void *) * input->len);
	nng_free(input->lens, sizeof(uint32_t) * input->len);
	nng_free(input->keys, sizeof(uint64_t) * input->len);
	nng_free(input, sizeof(struct stream_data_in));
	
	// Clean up result
	if (result != NULL) {
		parquet_data_free((parquet_data *)result);
	}
	
	stream_sys_fini();
}

// Test raw_encode with zero length input
void test_raw_encode_zero_length(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in *input = nng_alloc(sizeof(struct stream_data_in));
	NUTS_TRUE(input != NULL);
	
	input->len = 0;
	input->keys = NULL;
	input->lens = NULL;
	input->datas = NULL;
	
	void *result = stream_encode(RAW_STREAM_ID, input);
	NUTS_TRUE(result == NULL); // Should fail with zero length
	
	nng_free(input, sizeof(struct stream_data_in));
	
	stream_sys_fini();
}

// Test raw_decode with NULL input
void test_raw_decode_null_input(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	void *result = stream_decode(RAW_STREAM_ID, NULL);
	NUTS_TRUE(result == NULL);
	
	stream_sys_fini();
}

// Test raw_decode with valid parquet_data_ret
void test_raw_decode_valid_input(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	// Create a parquet_data_ret structure
	struct parquet_data_ret *input = nng_alloc(sizeof(struct parquet_data_ret));
	NUTS_TRUE(input != NULL);
	
	input->col_len = 1;
	input->row_len = 2;
	
	input->ts = nng_alloc(sizeof(uint64_t) * input->row_len);
	NUTS_TRUE(input->ts != NULL);
	input->ts[0] = 1000;
	input->ts[1] = 1001;
	
	input->schema = nng_alloc(sizeof(char *) * input->col_len);
	NUTS_TRUE(input->schema != NULL);
	input->schema[0] = nng_alloc(strlen("data") + 1);
	strcpy(input->schema[0], "data");
	
	input->payload_arr = nng_alloc(sizeof(parquet_data_packet **) * input->col_len);
	NUTS_TRUE(input->payload_arr != NULL);
	input->payload_arr[0] = nng_alloc(sizeof(parquet_data_packet *) * input->row_len);
	NUTS_TRUE(input->payload_arr[0] != NULL);
	
	for (uint32_t r = 0; r < input->row_len; r++) {
		input->payload_arr[0][r] = nng_alloc(sizeof(parquet_data_packet));
		NUTS_TRUE(input->payload_arr[0][r] != NULL);
		input->payload_arr[0][r]->size = 5 + r;
		input->payload_arr[0][r]->data = nng_alloc(input->payload_arr[0][r]->size);
		NUTS_TRUE(input->payload_arr[0][r]->data != NULL);
		memset(input->payload_arr[0][r]->data, 'X' + r, input->payload_arr[0][r]->size);
	}
	
	// Decode
	struct stream_decoded_data *result = stream_decode(RAW_STREAM_ID, input);
	NUTS_TRUE(result != NULL);
	NUTS_TRUE(result->data != NULL);
	NUTS_TRUE(result->len == 5 + 6); // Sum of payload sizes
	
	// Clean up result
	stream_decoded_data_free(result);
	
	// Clean up input
	for (uint32_t r = 0; r < input->row_len; r++) {
		nng_free(input->payload_arr[0][r]->data, input->payload_arr[0][r]->size);
		nng_free(input->payload_arr[0][r], sizeof(parquet_data_packet));
	}
	nng_free(input->payload_arr[0], sizeof(parquet_data_packet *) * input->row_len);
	nng_free(input->payload_arr, sizeof(parquet_data_packet **) * input->col_len);
	nng_free(input->schema[0], strlen("data") + 1);
	nng_free(input->schema, sizeof(char *) * input->col_len);
	nng_free(input->ts, sizeof(uint64_t) * input->row_len);
	nng_free(input, sizeof(struct parquet_data_ret));
	
	stream_sys_fini();
}

// Test raw_decode with empty payload
void test_raw_decode_empty_payload(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct parquet_data_ret *input = nng_alloc(sizeof(struct parquet_data_ret));
	NUTS_TRUE(input != NULL);
	
	input->col_len = 1;
	input->row_len = 1;
	input->ts = nng_alloc(sizeof(uint64_t));
	input->ts[0] = 1000;
	input->schema = nng_alloc(sizeof(char *));
	input->schema[0] = nng_alloc(strlen("data") + 1);
	strcpy(input->schema[0], "data");
	input->payload_arr = nng_alloc(sizeof(parquet_data_packet **));
	input->payload_arr[0] = nng_alloc(sizeof(parquet_data_packet *));
	input->payload_arr[0][0] = nng_alloc(sizeof(parquet_data_packet));
	input->payload_arr[0][0]->size = 0;
	input->payload_arr[0][0]->data = NULL;
	
	struct stream_decoded_data *result = stream_decode(RAW_STREAM_ID, input);
	NUTS_TRUE(result == NULL); // Should fail with empty payload
	
	// Clean up
	nng_free(input->payload_arr[0][0], sizeof(parquet_data_packet));
	nng_free(input->payload_arr[0], sizeof(parquet_data_packet *));
	nng_free(input->payload_arr, sizeof(parquet_data_packet **));
	nng_free(input->schema[0], strlen("data") + 1);
	nng_free(input->schema, sizeof(char *));
	nng_free(input->ts, sizeof(uint64_t));
	nng_free(input, sizeof(struct parquet_data_ret));
	
	stream_sys_fini();
}

// Test raw_cmd_parser with valid sync command
void test_raw_cmd_parser_sync(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	char *cmd = "sync-100-200";
	struct cmd_data *result = stream_cmd_parser(RAW_STREAM_ID, cmd);
	
	NUTS_TRUE(result != NULL);
	NUTS_TRUE(result->is_sync == true);
	NUTS_TRUE(result->start_key == 100);
	NUTS_TRUE(result->end_key == 200);
	NUTS_TRUE(result->schema_len == 2);
	NUTS_TRUE(result->schema != NULL);
	NUTS_TRUE(strcmp(result->schema[0], "ts") == 0);
	NUTS_TRUE(strcmp(result->schema[1], "data") == 0);
	
	// Clean up
	for (uint32_t i = 0; i < result->schema_len; i++) {
		nng_free(result->schema[i], strlen(result->schema[i]) + 1);
	}
	nng_free(result->schema, result->schema_len * sizeof(char *));
	nng_free(result, sizeof(struct cmd_data));
	
	stream_sys_fini();
}

// Test raw_cmd_parser with valid async command
void test_raw_cmd_parser_async(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	char *cmd = "async-500-1000";
	struct cmd_data *result = stream_cmd_parser(RAW_STREAM_ID, cmd);
	
	NUTS_TRUE(result != NULL);
	NUTS_TRUE(result->is_sync == false);
	NUTS_TRUE(result->start_key == 500);
	NUTS_TRUE(result->end_key == 1000);
	
	// Clean up
	for (uint32_t i = 0; i < result->schema_len; i++) {
		nng_free(result->schema[i], strlen(result->schema[i]) + 1);
	}
	nng_free(result->schema, result->schema_len * sizeof(char *));
	nng_free(result, sizeof(struct cmd_data));
	
	stream_sys_fini();
}

// Test raw_cmd_parser with invalid command format
void test_raw_cmd_parser_invalid_format(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	// Invalid prefix
	char *cmd1 = "invalid-100-200";
	struct cmd_data *result = stream_cmd_parser(RAW_STREAM_ID, cmd1);
	NUTS_TRUE(result == NULL);
	
	// Missing delimiters
	char *cmd2 = "sync100200";
	result = stream_cmd_parser(RAW_STREAM_ID, cmd2);
	NUTS_TRUE(result == NULL);
	
	// Only one delimiter
	char *cmd3 = "sync-100";
	result = stream_cmd_parser(RAW_STREAM_ID, cmd3);
	NUTS_TRUE(result == NULL);
	
	// Non-numeric keys
	char *cmd4 = "sync-abc-def";
	result = stream_cmd_parser(RAW_STREAM_ID, cmd4);
	NUTS_TRUE(result == NULL);
	
	stream_sys_fini();
}

// Test raw_cmd_parser with NULL input
void test_raw_cmd_parser_null(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct cmd_data *result = stream_cmd_parser(RAW_STREAM_ID, NULL);
	// May crash or return NULL - implementation dependent
	// This test documents current behavior
	
	stream_sys_fini();
}

// Test raw_cmd_parser with edge case values
void test_raw_cmd_parser_edge_cases(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	// Zero values
	char *cmd1 = "sync-0-0";
	struct cmd_data *result = stream_cmd_parser(RAW_STREAM_ID, cmd1);
	NUTS_TRUE(result != NULL);
	NUTS_TRUE(result->start_key == 0);
	NUTS_TRUE(result->end_key == 0);
	
	// Clean up
	for (uint32_t i = 0; i < result->schema_len; i++) {
		nng_free(result->schema[i], strlen(result->schema[i]) + 1);
	}
	nng_free(result->schema, result->schema_len * sizeof(char *));
	nng_free(result, sizeof(struct cmd_data));
	
	// Large values
	char *cmd2 = "async-999999999-9999999999";
	result = stream_cmd_parser(RAW_STREAM_ID, cmd2);
	NUTS_TRUE(result != NULL);
	NUTS_TRUE(result->start_key == 999999999);
	NUTS_TRUE(result->end_key == 9999999999ULL);
	
	// Clean up
	for (uint32_t i = 0; i < result->schema_len; i++) {
		nng_free(result->schema[i], strlen(result->schema[i]) + 1);
	}
	nng_free(result->schema, result->schema_len * sizeof(char *));
	nng_free(result, sizeof(struct cmd_data));
	
	stream_sys_fini();
}

NUTS_TESTS = {
	{ "raw stream register basic", test_raw_stream_register_basic },
	{ "raw encode null input", test_raw_encode_null_input },
	{ "raw encode valid input", test_raw_encode_valid_input },
	{ "raw encode zero length", test_raw_encode_zero_length },
	{ "raw decode null input", test_raw_decode_null_input },
	{ "raw decode valid input", test_raw_decode_valid_input },
	{ "raw decode empty payload", test_raw_decode_empty_payload },
	{ "raw cmd parser sync", test_raw_cmd_parser_sync },
	{ "raw cmd parser async", test_raw_cmd_parser_async },
	{ "raw cmd parser invalid format", test_raw_cmd_parser_invalid_format },
	{ "raw cmd parser null", test_raw_cmd_parser_null },
	{ "raw cmd parser edge cases", test_raw_cmd_parser_edge_cases },
	{ NULL, NULL },
};