// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include "nng/exchange/stream/stream.h"
#include "nng/exchange/stream/raw_stream.h"
#include "nng/mqtt/mqtt_client.h"
#include <nuts.h>
#include <string.h>

#define UNUSED(x) ((void) x)

#define RAW_STREAM_ID 0x0
#define INVALID_STREAM_ID 0xf
#define TEST_STREAM_ID 0x10

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
	input.lens = nng_alloc(sizeof(uint32_t) * 2);
	input.datas = nng_alloc(sizeof(void*) * 2);
	
	NUTS_ASSERT(input.keys != NULL);
	NUTS_ASSERT(input.lens != NULL);
	NUTS_ASSERT(input.datas != NULL);
	
	input.keys[0] = 1000;
	input.keys[1] = 2000;
	input.lens[0] = 4;
	input.lens[1] = 8;
	input.datas[0] = nng_alloc(4);
	input.datas[1] = nng_alloc(8);
	
	memcpy(input.datas[0], "test", 4);
	memcpy(input.datas[1], "testdata", 8);
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	nng_free(input.datas[0], 4);
	nng_free(input.datas[1], 8);
	nng_free(input.datas, sizeof(void*) * 2);
	nng_free(input.lens, sizeof(uint32_t) * 2);
	nng_free(input.keys, sizeof(uint64_t) * 2);
	
	stream_sys_fini();
}

void test_stream_encode_empty_data(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	input.len = 0;
	input.keys = NULL;
	input.lens = NULL;
	input.datas = NULL;
	
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
	input.lens = nng_alloc(sizeof(uint32_t));
	input.datas = nng_alloc(sizeof(void*));
	
	input.keys[0] = 12345;
	input.lens[0] = 5;
	input.datas[0] = nng_alloc(5);
	memcpy(input.datas[0], "hello", 5);
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	nng_free(input.datas[0], 5);
	nng_free(input.datas, sizeof(void*));
	nng_free(input.lens, sizeof(uint32_t));
	nng_free(input.keys, sizeof(uint64_t));
	
	stream_sys_fini();
}

void test_stream_encode_large_payload(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	size_t large_size = 1024; // 1KB
	
	input.len = 1;
	input.keys = nng_alloc(sizeof(uint64_t));
	input.lens = nng_alloc(sizeof(uint32_t));
	input.datas = nng_alloc(sizeof(void*));
	
	input.keys[0] = 99999;
	input.lens[0] = large_size;
	input.datas[0] = nng_alloc(large_size);
	
	if (input.datas[0] != NULL) {
		memset(input.datas[0], 0xAA, large_size);
		
		void *output = stream_encode(RAW_STREAM_ID, &input);
		
		if (output != NULL) {
			parquet_data_free((parquet_data*)output);
		}
		
		nng_free(input.datas[0], large_size);
	}
	
	nng_free(input.datas, sizeof(void*));
	nng_free(input.lens, sizeof(uint32_t));
	nng_free(input.keys, sizeof(uint64_t));
	
	stream_sys_fini();
}

static nng_msg *
mqtt_publish_msg(const char *topic, const uint8_t *payload, uint32_t plen,
    uint64_t ts)
{
	nng_msg *msg = NULL;
	uint8_t *header;

	if (nng_mqtt_msg_alloc(&msg, 0) != 0) {
		return NULL;
	}
	header  = nng_msg_header(msg);
	*header = (uint8_t) (*header | CMD_PUBLISH);
	nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(msg, false);
	nng_mqtt_msg_set_publish_qos(msg, 0);
	nng_mqtt_msg_set_publish_retain(msg, false);
	nng_mqtt_msg_set_publish_topic(msg, topic);
	nng_mqtt_msg_set_publish_topic_len(msg, (uint32_t) strlen(topic));
	if (nng_mqtt_msg_set_publish_payload(msg, (uint8_t *) payload, plen) != 0) {
		nng_msg_free(msg);
		return NULL;
	}
	nng_msg_set_timestamp(msg, ts);
	if (nng_mqtt_msg_encode(msg) != 0) {
		nng_msg_free(msg);
		return NULL;
	}
	/* The wire body starts with the topic. Point the legacy cursor there
	 * so a regression to nng_msg_payload_ptr cannot still match. */
	nng_msg_set_payload_ptr(msg, nng_msg_body(msg));
	return msg;
}

void test_stream_msg_payload_mqtt_publish(void)
{
	const char *topic = "vehicle/speed/front";
	const uint8_t payload0[] = "mqtt-payload-42";
	const uint8_t payload1[] = "second-row";
	const uint32_t len0 = (uint32_t) strlen((const char *) payload0);
	const uint32_t len1 = (uint32_t) strlen((const char *) payload1);
	nng_msg *msgs[2];
	uint32_t plen = 99;
	uint8_t *got;
	struct stream_data_in input;
	/* parquet_data is opaque when SUPP_PARQUET is on. Same field order
	 * as the object raw_encode returns. */
	struct {
		uint32_t               col_len;
		uint32_t               row_len;
		uint64_t              *ts;
		char                 **schema;
		parquet_data_packet ***payload_arr;
	} *out;

	NUTS_TRUE(stream_msg_payload(NULL, &plen) == NULL);
	NUTS_TRUE(plen == 0);
	NUTS_TRUE(stream_msg_payload(NULL, NULL) == NULL);

	msgs[0] = mqtt_publish_msg(topic, payload0, len0, 1000);
	msgs[1] = mqtt_publish_msg(topic, payload1, len1, 2000);
	NUTS_TRUE(msgs[0] != NULL);
	NUTS_TRUE(msgs[1] != NULL);
	NUTS_TRUE(nng_msg_len(msgs[0]) > len0);

	got = stream_msg_payload(msgs[0], &plen);
	NUTS_TRUE(got != NULL);
	NUTS_TRUE(plen == len0);
	NUTS_TRUE(memcmp(got, payload0, len0) == 0);
	NUTS_TRUE(got != nng_msg_body(msgs[0]));
	NUTS_TRUE(got != nng_msg_payload_ptr(msgs[0]));

	NUTS_TRUE(stream_sys_init() == 0);

	input.len   = 2;
	input.keys  = nng_alloc(sizeof(uint64_t) * 2);
	input.lens  = nng_alloc(sizeof(uint32_t) * 2);
	input.datas = nng_alloc(sizeof(void *) * 2);
	NUTS_TRUE(input.keys != NULL);
	NUTS_TRUE(input.lens != NULL);
	NUTS_TRUE(input.datas != NULL);

	for (uint32_t i = 0; i < 2; i++) {
		input.keys[i] = nng_msg_get_timestamp(msgs[i]);
		input.datas[i] = stream_msg_payload(msgs[i], &input.lens[i]);
	}

	out = stream_encode(RAW_STREAM_ID, &input);
	NUTS_TRUE(out != NULL);
	NUTS_TRUE(out->row_len == 2);
	NUTS_TRUE(out->ts[0] == 1000);
	NUTS_TRUE(out->ts[1] == 2000);
	NUTS_TRUE(out->payload_arr[0][0] != NULL);
	NUTS_TRUE(out->payload_arr[0][0]->size == len0);
	NUTS_TRUE(memcmp(out->payload_arr[0][0]->data, payload0, len0) == 0);
	NUTS_TRUE(out->payload_arr[0][1] != NULL);
	NUTS_TRUE(out->payload_arr[0][1]->size == len1);
	NUTS_TRUE(memcmp(out->payload_arr[0][1]->data, payload1, len1) == 0);
	NUTS_TRUE(memcmp(out->payload_arr[0][0]->data, nng_msg_body(msgs[0]),
	              len0) != 0);

	parquet_data_free((parquet_data *) out);
	nng_free(input.datas, sizeof(void *) * 2);
	nng_free(input.lens, sizeof(uint32_t) * 2);
	nng_free(input.keys, sizeof(uint64_t) * 2);
	nng_msg_free(msgs[0]);
	nng_msg_free(msgs[1]);
	stream_sys_fini();
}

void test_stream_encode_zero_length_payload(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);
	
	struct stream_data_in input;
	input.len = 1;
	input.keys = nng_alloc(sizeof(uint64_t));
	input.lens = nng_alloc(sizeof(uint32_t));
	input.datas = nng_alloc(sizeof(void*));
	
	input.keys[0] = 100;
	input.lens[0] = 0;
	input.datas[0] = NULL;
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	nng_free(input.datas, sizeof(void*));
	nng_free(input.lens, sizeof(uint32_t));
	nng_free(input.keys, sizeof(uint64_t));
	
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
	input.lens = nng_alloc(sizeof(uint32_t) * count);
	input.datas = nng_alloc(sizeof(void*) * count);
	
	for (uint32_t i = 0; i < count; i++) {
		input.keys[i] = i * 1000;
		input.lens[i] = (i % 10) + 1;
		input.datas[i] = nng_alloc(input.lens[i]);
		memset(input.datas[i], i & 0xFF, input.lens[i]);
	}
	
	void *output = stream_encode(RAW_STREAM_ID, &input);
	
	if (output != NULL) {
		parquet_data_free((parquet_data*)output);
	}
	
	for (uint32_t i = 0; i < count; i++) {
		nng_free(input.datas[i], input.lens[i]);
	}
	nng_free(input.datas, sizeof(void*) * count);
	nng_free(input.lens, sizeof(uint32_t) * count);
	nng_free(input.keys, sizeof(uint64_t) * count);
	
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
	{ "stream msg payload mqtt publish", test_stream_msg_payload_mqtt_publish },
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
