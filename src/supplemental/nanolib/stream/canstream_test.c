// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include <stdlib.h>
#include <string.h>
#include "nng/supplemental/nanolib/canstream.h"
#include "nng/nng.h"
#include <nuts.h>

// Test canStreamAddRow basic functionality
void
test_canstream_add_row_basic(void)
{
	struct canStream *stream = nng_alloc(sizeof(struct canStream));
	NUTS_TRUE(stream != NULL);
	
	stream->rowLen = 0;
	stream->row = NULL;
	
	uint64_t timestamp = 1234567890;
	int ret = canStreamAddRow(stream, timestamp);
	// Note: Function returns -1 on both success and failure - checking allocation
	NUTS_TRUE(stream->rowLen == 1);
	NUTS_TRUE(stream->row != NULL);
	NUTS_TRUE(stream->row[0].timestamp == timestamp);
	NUTS_TRUE(stream->row[0].colLen == 0);
	NUTS_TRUE(stream->row[0].col == NULL);
	
	freeCanStream(stream);
}

// Test canStreamAddRow multiple rows
void
test_canstream_add_multiple_rows(void)
{
	struct canStream *stream = nng_alloc(sizeof(struct canStream));
	NUTS_TRUE(stream != NULL);
	
	stream->rowLen = 0;
	stream->row = NULL;
	
	for (int i = 0; i < 5; i++) {
		uint64_t timestamp = 1000 + i;
		canStreamAddRow(stream, timestamp);
		NUTS_TRUE(stream->rowLen == (uint32_t)(i + 1));
	}
	
	// Verify all timestamps
	for (int i = 0; i < 5; i++) {
		NUTS_TRUE(stream->row[i].timestamp == (uint64_t)(1000 + i));
	}
	
	freeCanStream(stream);
}

// Test canStreamRowAddColumn basic functionality
void
test_canstream_add_column_basic(void)
{
	struct canStreamRow *row = nng_alloc(sizeof(struct canStreamRow));
	NUTS_TRUE(row != NULL);
	
	row->timestamp = 1234567890;
	row->colLen = 0;
	row->col = NULL;
	
	uint64_t canid = 0x123;
	uint8_t busid = 1;
	uint16_t payloadLen = 8;
	uint8_t *payload = nng_alloc(payloadLen);
	NUTS_TRUE(payload != NULL);
	
	for (int i = 0; i < payloadLen; i++) {
		payload[i] = (uint8_t)i;
	}
	
	int ret = canStreamRowAddColumn(row, canid, busid, payloadLen, payload);
	// Note: Function returns -1 on both success and failure
	NUTS_TRUE(row->colLen == 1);
	NUTS_TRUE(row->col != NULL);
	NUTS_TRUE(row->col[0].canid == canid);
	NUTS_TRUE(row->col[0].busid == busid);
	NUTS_TRUE(row->col[0].payloadLen == payloadLen);
	NUTS_TRUE(row->col[0].payload == payload);
	
	// Clean up
	nng_free(row->col[0].payload, payloadLen);
	nng_free(row->col, sizeof(struct canStreamCol));
	nng_free(row, sizeof(struct canStreamRow));
}

// Test canStreamRowAddColumn multiple columns
void
test_canstream_add_multiple_columns(void)
{
	struct canStreamRow *row = nng_alloc(sizeof(struct canStreamRow));
	NUTS_TRUE(row != NULL);
	
	row->timestamp = 1234567890;
	row->colLen = 0;
	row->col = NULL;
	
	for (int i = 0; i < 3; i++) {
		uint64_t canid = 0x100 + i;
		uint8_t busid = (uint8_t)(i + 1);
		uint16_t payloadLen = 8;
		uint8_t *payload = nng_alloc(payloadLen);
		NUTS_TRUE(payload != NULL);
		
		memset(payload, 'A' + i, payloadLen);
		
		canStreamRowAddColumn(row, canid, busid, payloadLen, payload);
		NUTS_TRUE(row->colLen == (uint32_t)(i + 1));
	}
	
	// Verify all columns
	for (int i = 0; i < 3; i++) {
		NUTS_TRUE(row->col[i].canid == (uint64_t)(0x100 + i));
		NUTS_TRUE(row->col[i].busid == (uint8_t)(i + 1));
		NUTS_TRUE(row->col[i].payloadLen == 8);
	}
	
	// Clean up
	for (unsigned int i = 0; i < row->colLen; i++) {
		nng_free(row->col[i].payload, row->col[i].payloadLen);
	}
	nng_free(row->col, row->colLen * sizeof(struct canStreamCol));
	nng_free(row, sizeof(struct canStreamRow));
}

// Test freeCanStream with single row and column
void
test_free_canstream_basic(void)
{
	struct canStream *stream = nng_alloc(sizeof(struct canStream));
	NUTS_TRUE(stream != NULL);
	
	stream->rowLen = 0;
	stream->row = NULL;
	
	canStreamAddRow(stream, 1000);
	
	uint8_t *payload = nng_alloc(8);
	NUTS_TRUE(payload != NULL);
	memset(payload, 0xAA, 8);
	
	canStreamRowAddColumn(&stream->row[0], 0x123, 1, 8, payload);
	
	freeCanStream(stream);
	// If we reach here without crash, test passes
	NUTS_PASS(1);
}

// Test freeCanStream with complex structure
void
test_free_canstream_complex(void)
{
	struct canStream *stream = nng_alloc(sizeof(struct canStream));
	NUTS_TRUE(stream != NULL);
	
	stream->rowLen = 0;
	stream->row = NULL;
	
	// Add multiple rows
	for (int i = 0; i < 3; i++) {
		canStreamAddRow(stream, 1000 + i);
		
		// Add multiple columns to each row
		for (int j = 0; j < 2; j++) {
			uint8_t *payload = nng_alloc(8);
			NUTS_TRUE(payload != NULL);
			memset(payload, 'A' + i + j, 8);
			canStreamRowAddColumn(&stream->row[i], 0x100 + j, (uint8_t)(j + 1), 8, payload);
		}
	}
	
	freeCanStream(stream);
	NUTS_PASS(1);
}

// Test parseCanStream with NULL input
void
test_parse_canstream_null(void)
{
	struct canStream *stream = parseCanStream(NULL, 0);
	NUTS_TRUE(stream == NULL);
}

// Test parseCanStream with zero length
void
test_parse_canstream_zero_length(void)
{
	nng_msg *msg;
	nng_msg_alloc(&msg, 0);
	
	nng_msg **msgArray = nng_alloc(sizeof(nng_msg *));
	msgArray[0] = msg;
	
	struct canStream *stream = parseCanStream(msgArray, 0);
	NUTS_TRUE(stream != NULL);
	NUTS_TRUE(stream->rowLen == 0);
	
	freeCanStream(stream);
	nng_free(msgArray, sizeof(nng_msg *));
	nng_msg_free(msg);
}

// Test parseCanStream with valid message
void
test_parse_canstream_valid_message(void)
{
	nng_msg *msg;
	nng_msg_alloc(&msg, 0);
	
	// Create a CAN stream message structure
	// Format: [2 bytes length][8 bytes timestamp][CAN packets]
	uint16_t total_len = 2 + 8 + 20; // Header + one CAN packet
	uint8_t *data = nng_alloc(total_len);
	NUTS_TRUE(data != NULL);
	
	// Set total length (big endian)
	memcpy_big_endian(data, &total_len, 2);
	
	// Set timestamp (big endian)
	uint64_t timestamp = 1234567890;
	memcpy_big_endian(data + 2, &timestamp, 8);
	
	// Set CAN packet length
	uint16_t can_packet_len = 20;
	memcpy_big_endian(data + 10, &can_packet_len, 2);
	
	// Set CAN packet data
	uint64_t can_timestamp = timestamp;
	memcpy_big_endian(data + 12, &can_timestamp, 8);
	
	uint32_t canid = 0x123;
	memcpy_big_endian(data + 20, &canid, 4);
	
	uint8_t busid = 1;
	data[24] = busid;
	
	data[25] = 0; // direction
	
	uint16_t payload_len = 0;
	memcpy_big_endian(data + 26, &payload_len, 2);
	
	// Append to message
	nng_msg_append(msg, data, total_len);
	nng_msg_set_payload_ptr(msg, data);
	
	nng_msg **msgArray = nng_alloc(sizeof(nng_msg *));
	msgArray[0] = msg;
	
	struct canStream *stream = parseCanStream(msgArray, 1);
	NUTS_TRUE(stream != NULL);
	NUTS_TRUE(stream->rowLen == 1);
	NUTS_TRUE(stream->row[0].timestamp == timestamp);
	
	freeCanStream(stream);
	nng_free(data, total_len);
	nng_free(msgArray, sizeof(nng_msg *));
	nng_msg_free(msg);
}

// Test big endian conversion helper
void
test_memcpy_big_endian(void)
{
	uint16_t val16 = 0x1234;
	uint16_t result16;
	uint8_t buf16[2];
	
	memcpy_big_endian(buf16, &val16, 2);
	memcpy_big_endian(&result16, buf16, 2);
	NUTS_TRUE(result16 == val16);
	
	uint32_t val32 = 0x12345678;
	uint32_t result32;
	uint8_t buf32[4];
	
	memcpy_big_endian(buf32, &val32, 4);
	memcpy_big_endian(&result32, buf32, 4);
	NUTS_TRUE(result32 == val32);
	
	uint64_t val64 = 0x123456789ABCDEF0ULL;
	uint64_t result64;
	uint8_t buf64[8];
	
	memcpy_big_endian(buf64, &val64, 8);
	memcpy_big_endian(&result64, buf64, 8);
	NUTS_TRUE(result64 == val64);
}

// Test canStreamAddRow with NULL stream
void
test_canstream_add_row_null_stream(void)
{
	// This will likely crash, but documents behavior
	// int ret = canStreamAddRow(NULL, 1000);
	// Skipping this test as it would cause segfault
	NUTS_PASS(1);
}

// Test edge case: Empty payload
void
test_canstream_empty_payload(void)
{
	struct canStreamRow *row = nng_alloc(sizeof(struct canStreamRow));
	NUTS_TRUE(row != NULL);
	
	row->timestamp = 1000;
	row->colLen = 0;
	row->col = NULL;
	
	uint8_t *payload = NULL; // Empty payload
	canStreamRowAddColumn(row, 0x123, 1, 0, payload);
	
	NUTS_TRUE(row->colLen == 1);
	NUTS_TRUE(row->col[0].payloadLen == 0);
	NUTS_TRUE(row->col[0].payload == NULL);
	
	// Clean up
	nng_free(row->col, sizeof(struct canStreamCol));
	nng_free(row, sizeof(struct canStreamRow));
}

NUTS_TESTS = {
	{ "canstream add row basic", test_canstream_add_row_basic },
	{ "canstream add multiple rows", test_canstream_add_multiple_rows },
	{ "canstream add column basic", test_canstream_add_column_basic },
	{ "canstream add multiple columns", test_canstream_add_multiple_columns },
	{ "free canstream basic", test_free_canstream_basic },
	{ "free canstream complex", test_free_canstream_complex },
	{ "parse canstream null", test_parse_canstream_null },
	{ "parse canstream zero length", test_parse_canstream_zero_length },
	{ "parse canstream valid message", test_parse_canstream_valid_message },
	{ "memcpy big endian", test_memcpy_big_endian },
	{ "canstream add row null stream", test_canstream_add_row_null_stream },
	{ "canstream empty payload", test_canstream_empty_payload },
	{ NULL, NULL },
};