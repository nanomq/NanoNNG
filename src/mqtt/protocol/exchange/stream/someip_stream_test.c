// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT

#include "nng/exchange/stream/someip_stream.h"
#include "someip_batch.h"
#include <nuts.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEST_ROWS = 6, TEST_WINDOWS = 3, TEST_FRAME_MAX = 128 };

static void
put_be(uint8_t *p, unsigned width, uint64_t value)
{
	for (unsigned i = 0; i < width; i++) {
		p[i] = (uint8_t) (value >> (8 * (width - i - 1)));
	}
}

static void
make_frame(uint8_t *p, uint32_t size, unsigned row, uint16_t session)
{
	memset(p, 0, TEST_FRAME_MAX);
	put_be(p, 2, row == 4 ? 0xffff : 0x1234 + row % 2);
	put_be(p + 2, 2, row == 4 ? 0x8100 : 0x8001 + row % 2);
	put_be(p + 4, 4, size - 8);
	put_be(p + 8, 2, 0x4321);
	put_be(p + 10, 2, session);
	p[12] = 1;
	p[13] = (uint8_t) (row + 1);
	p[14] = row == 5 ? 0x22 : 0x02;
	p[15] = (uint8_t) row;
	for (uint32_t i = 16; i < size; i++) {
		p[i] = (uint8_t) (i * 17 + row * 29);
	}
}

static void
make_records(someip_record records[TEST_ROWS],
    uint8_t frames[TEST_ROWS][TEST_FRAME_MAX], bool wide, unsigned pattern)
{
	static const uint32_t sizes[] = { 16, 32, 80, 17, 48, 128 };
	static const uint64_t times[] = { 0, UINT32_MAX, 100, 1, 0, 999 };
	static const uint16_t sessions[][TEST_ROWS] = {
		{ 65534, 65535, 0, 1, 2, 3 },
		{ 0, 100, 1, 101, 2, 102 },
		{ 1, 2, 10, 11, 12, 13 },
	};
	static const uint64_t windows[] = { 10, 12, 20 };
	uint64_t base = wide ? UINT64_C(1) << 48 : 0;
	for (unsigned i = 0; i < TEST_ROWS; i++) {
		make_frame(frames[i], sizes[i], i, sessions[pattern][i]);
		records[i] = (someip_record) {
			.ts = times[i], .batch_index = base + windows[i / 2],
			.frame_index = i % 2, .frame = frames[i], .size = sizes[i],
		};
	}
}

static someip_batch *
batch_of(parquet_data *encoded)
{
	NUTS_TRUE(encoded != NULL);
	struct stream_data_out *out = (struct stream_data_out *) encoded;
	NUTS_TRUE(out->someip_data != NULL);
	return out->someip_data;
}

static void
check_text(struct stream_decoded_data *text, const someip_record *records,
    unsigned count)
{
	static const char hex[] = "0123456789abcdef";
	char prefix[128];
	size_t pos = 0;
	NUTS_TRUE(text != NULL);
	NUTS_TRUE(text->data != NULL);
	for (unsigned i = 0; i < count; i++) {
		int n = snprintf(prefix, sizeof(prefix),
		    "ts=%" PRIu64 ",batch_index=%" PRIu64
		    ",frame_index=%u,someip=",
		    records[i].ts, records[i].batch_index, records[i].frame_index);
		NUTS_TRUE(n > 0 && (size_t) n < sizeof(prefix));
		NUTS_TRUE(pos + n + 2 * records[i].size + 1 <= text->len);
		NUTS_TRUE(memcmp((char *) text->data + pos, prefix, n) == 0);
		pos += (size_t) n;
		for (uint32_t j = 0; j < records[i].size; j++) {
			NUTS_TRUE(((char *) text->data)[pos++] == hex[records[i].frame[j] >> 4]);
			NUTS_TRUE(((char *) text->data)[pos++] == hex[records[i].frame[j] & 15]);
		}
		NUTS_TRUE(((char *) text->data)[pos++] == '\n');
	}
	NUTS_TRUE(pos == text->len);
	NUTS_TRUE(((char *) text->data)[pos] == '\0');
	stream_decoded_data_free(text);
}

static nng_msg *
make_window(const someip_record *records, unsigned count)
{
	static const uint8_t topic[] = { 0, 6, 'c', 'a', 'n', 'u', 'd', 'p' };
	uint8_t payload[4 + TEST_ROWS * (6 + TEST_FRAME_MAX)];
	uint32_t pos = 4;
	put_be(payload, 4, count);
	for (unsigned i = 0; i < count; i++) {
		put_be(payload + pos, 4, records[i].ts);
		put_be(payload + pos + 4, 2, records[i].size);
		pos += 6;
		memcpy(payload + pos, records[i].frame, records[i].size);
		pos += records[i].size;
	}
	nng_msg *msg = NULL;
	NUTS_PASS(nng_msg_alloc(&msg, 0));
	NUTS_PASS(nng_msg_append(msg, topic, sizeof(topic)));
	NUTS_PASS(nng_msg_append(msg, payload, pos));
	nng_msg_set_payload_ptr(msg, (uint8_t *) nng_msg_body(msg) + sizeof(topic));
	return msg;
}

static void
check_equal_batch(const someip_batch *a, const someip_batch *b)
{
	NUTS_TRUE(a->rows == b->rows);
	NUTS_TRUE(a->template_size == b->template_size);
	NUTS_TRUE(a->session_delta == b->session_delta);
	NUTS_TRUE(a->version == b->version);
	NUTS_TRUE(strcmp(a->manifest, b->manifest) == 0);
	NUTS_TRUE(memcmp(a->ts, b->ts, a->rows * sizeof(*a->ts)) == 0);
	NUTS_TRUE(memcmp(a->batch_index, b->batch_index,
	    a->rows * sizeof(*a->batch_index)) == 0);
	NUTS_TRUE(memcmp(a->frame_index, b->frame_index,
	    a->rows * sizeof(*a->frame_index)) == 0);
	NUTS_TRUE(memcmp(a->session_id, b->session_id,
	    a->rows * sizeof(*a->session_id)) == 0);
	NUTS_TRUE(memcmp(a->offsets, b->offsets,
	    (a->rows + 1) * sizeof(*a->offsets)) == 0);
	NUTS_TRUE(memcmp(a->templates, b->templates, a->template_size) == 0);
	NUTS_TRUE(someip_batch_validate(a) == 0);
	NUTS_TRUE(someip_batch_validate(b) == 0);
}

static void
test_window_record_equivalence(void)
{
	NUTS_PASS(stream_sys_init());
	for (unsigned wide = 0; wide < 2; wide++) {
		for (unsigned pattern = 0; pattern < 3; pattern++) {
			uint8_t frames[TEST_ROWS][TEST_FRAME_MAX];
			uint8_t saved_frames[TEST_ROWS][TEST_FRAME_MAX];
			someip_record records[TEST_ROWS], saved_records[TEST_ROWS];
			make_records(records, frames, wide != 0, pattern);
			memcpy(saved_frames, frames, sizeof(frames));
			memcpy(saved_records, records, sizeof(records));
			void *messages[TEST_WINDOWS];
			uint64_t keys[TEST_WINDOWS];
			uint8_t saved_messages[TEST_WINDOWS][512];
			size_t sizes[TEST_WINDOWS];
			for (unsigned w = 0; w < TEST_WINDOWS; w++) {
				messages[w] = make_window(records + 2 * w, 2);
				keys[w] = records[2 * w].batch_index;
				sizes[w] = nng_msg_len(messages[w]);
				NUTS_TRUE(sizes[w] <= sizeof(saved_messages[w]));
				memcpy(saved_messages[w], nng_msg_body(messages[w]), sizes[w]);
			}
			struct stream_data_in in = {
				.msgs = messages, .keys = keys, .len = TEST_WINDOWS,
			};
			for (unsigned checked = 0; checked < 2; checked++) {
				uint8_t id = checked ? SOMEIP_CHECKED_STREAM_ID : SOMEIP_STREAM_ID;
				parquet_data *direct = someip_encode_records(records, TEST_ROWS, checked != 0);
				parquet_data *window = stream_encode(id, &in);
				someip_batch *b = batch_of(window);
				check_equal_batch(batch_of(direct), b);
				NUTS_TRUE(b->session_delta == (pattern == 0));
				NUTS_TRUE(b->version == (checked ? (wide ? 3 : 2) : 0));
				NUTS_TRUE(((struct stream_data_out *) window)->ts == b->batch_index);
				char **schema = someip_batch_schema(b);
				NUTS_TRUE(strcmp(schema[0], "ts") == 0);
				NUTS_TRUE(strcmp(schema[1], "batch_index") == 0);
				NUTS_TRUE(strcmp(schema[2], "frame_index") == 0);
				NUTS_TRUE(strcmp(schema[3], "session_id") == 0);
				NUTS_TRUE(strcmp(schema[4], checked ? (wide ?
				    "message_template_v3" : "message_template_v2") : "message_template") == 0);
				check_text(someip_decode_range(window, 0, INT64_MAX), records, TEST_ROWS);
				check_text(someip_decode_range(window, keys[1], keys[1]), records + 2, 2);
				check_text(someip_decode_range(window, keys[0] + 1, keys[0] + 1), NULL, 0);
				NUTS_TRUE(someip_decode_range(window, keys[1], keys[0]) == NULL);
				NUTS_TRUE(memcmp(frames, saved_frames, sizeof(frames)) == 0);
				NUTS_TRUE(memcmp(records, saved_records, sizeof(records)) == 0);
				for (unsigned w = 0; w < TEST_WINDOWS; w++) {
					NUTS_TRUE(nng_msg_len(messages[w]) == sizes[w]);
					NUTS_TRUE(memcmp(nng_msg_body(messages[w]), saved_messages[w], sizes[w]) == 0);
				}
				parquet_data_free(window);
				parquet_data_free(direct);
			}
			for (unsigned w = 0; w < TEST_WINDOWS; w++) {
				nng_msg_free(messages[w]);
			}
		}
	}
	stream_sys_fini();
}

static void
test_input_rejection(void)
{
	uint8_t frames[TEST_ROWS][TEST_FRAME_MAX];
	someip_record records[TEST_ROWS];
	make_records(records, frames, false, 0);
	for (unsigned checked = 0; checked < 2; checked++) {
		NUTS_TRUE(someip_encode_records(NULL, 1, checked) == NULL);
		NUTS_TRUE(someip_encode_records(records, 0, checked) == NULL);
		someip_record saved = records[TEST_ROWS - 1];
		records[TEST_ROWS - 1].size = 0;
		NUTS_TRUE(someip_encode_records(records, TEST_ROWS, checked) == NULL);
		records[TEST_ROWS - 1] = saved;
		records[TEST_ROWS - 1].ts = UINT64_MAX;
		NUTS_TRUE(someip_encode_records(records, TEST_ROWS, checked) == NULL);
		records[TEST_ROWS - 1] = saved;
		records[TEST_ROWS - 1].batch_index = UINT64_MAX;
		NUTS_TRUE(someip_encode_records(records, TEST_ROWS, checked) == NULL);
		records[TEST_ROWS - 1] = saved;
		put_be(frames[TEST_ROWS - 1] + 4, 4, UINT32_MAX);
		NUTS_TRUE(someip_encode_records(records, TEST_ROWS, checked) == NULL);
		put_be(frames[TEST_ROWS - 1] + 4, 4, saved.size - 8);
	}
	void *messages[2] = { make_window(records, 2), make_window(records + 2, 2) };
	uint64_t keys[2] = { 10, 12 };
	struct stream_data_in in = { .msgs = messages, .keys = keys, .len = 2 };
	for (unsigned mode = 0; mode < 6; mode++) {
		nng_msg *bad = make_window(records + 2, 2);
		uint8_t *payload = nng_msg_payload_ptr(bad);
		if (mode == 0) put_be(payload, 4, 0);
		if (mode == 1) put_be(payload, 4, UINT32_MAX);
		if (mode == 2) put_be(payload + 4 + 6 + 4, 4, UINT32_MAX);
		if (mode == 3) {
			/* The final zero-sized PDU must invalidate its valid prefix. */
			put_be(payload + 4 + 6 + records[2].size + 4, 2, 0);
		}
		if (mode == 4) NUTS_PASS(nng_msg_chop(bad, 1));
		if (mode == 5) {
			const uint8_t extra = 0;
			NUTS_PASS(nng_msg_append(bad, &extra, 1));
			/* Appending may move the body buffer. */
			nng_msg_set_payload_ptr(bad, (uint8_t *) nng_msg_body(bad) + 8);
		}
		void *saved = messages[1];
		messages[1] = bad;
		NUTS_TRUE(someip_encode(&in) == NULL);
		NUTS_TRUE(someip_checked_encode(&in) == NULL);
		messages[1] = saved;
		nng_msg_free(bad);
	}
	keys[1] = keys[0];
	NUTS_TRUE(someip_encode(&in) == NULL);
	keys[1] = UINT64_MAX;
	NUTS_TRUE(someip_checked_encode(&in) == NULL);
	for (unsigned i = 0; i < 2; i++) nng_msg_free(messages[i]);
}

static void
test_integrity_and_structure(void)
{
	uint8_t frames[TEST_ROWS][TEST_FRAME_MAX];
	someip_record records[TEST_ROWS];
	for (unsigned wide = 0; wide < 2; wide++) {
		make_records(records, frames, wide != 0, 1);
		for (unsigned checked = 0; checked < 2; checked++) {
			parquet_data *encoded = someip_encode_records(records, TEST_ROWS, checked);
			someip_batch *b = batch_of(encoded);
			uint64_t selected = records[2].batch_index;
			NUTS_TRUE(someip_batch_validate(b) == 0);
			/* Header byte corruption lies outside the requested window. */
			b->templates[10] = 1;
			NUTS_TRUE(someip_decode_range(encoded, selected, selected) == NULL);
			NUTS_TRUE(someip_decode_range(encoded, selected + 1, selected + 1) == NULL);
			b->templates[10] = 0;
			uint32_t saved_offset = b->offsets[TEST_ROWS];
			b->offsets[TEST_ROWS] = b->template_size + 1;
			NUTS_TRUE(someip_batch_validate(b) != 0);
			b->offsets[TEST_ROWS] = saved_offset;
			int32_t saved_session = b->session_id[0];
			b->session_id[0] = -1;
			NUTS_TRUE(someip_batch_validate(b) != 0);
			b->session_id[0] = 65536;
			NUTS_TRUE(someip_batch_validate(b) != 0);
			b->session_id[0] = saved_session;
			uint64_t saved_time = b->ts[0];
			b->ts[0] = UINT64_MAX;
			NUTS_TRUE(someip_batch_validate(b) != 0);
			b->ts[0] = saved_time;
			uint64_t saved_frame = b->frame_index[0];
			b->frame_index[0] = (uint64_t) UINT32_MAX + 1;
			NUTS_TRUE(someip_batch_validate(b) != 0);
			b->frame_index[0] = saved_frame;
			if (checked) {
				/* This changes a valid field, so only the full SHA catches it. */
				b->templates[0] ^= 1;
				NUTS_TRUE(someip_decode_range(encoded, selected, selected) == NULL);
				NUTS_TRUE(someip_decode_range(encoded, selected + 1, selected + 1) == NULL);
				b->templates[0] ^= 1;
				char saved_manifest[sizeof(b->manifest)];
				memcpy(saved_manifest, b->manifest, sizeof(saved_manifest));
				b->manifest[0] = '\0';
				NUTS_TRUE(someip_batch_validate(b) != 0);
				memcpy(b->manifest, saved_manifest, sizeof(saved_manifest));
				b->manifest[30] = b->manifest[30] == '0' ? '1' : '0';
				NUTS_TRUE(someip_batch_validate(b) != 0);
				memcpy(b->manifest, saved_manifest, sizeof(saved_manifest));
			}
			uint8_t saved_version = b->version;
			b->version = 4;
			NUTS_TRUE(someip_batch_validate(b) != 0);
			b->version = saved_version;
			check_text(someip_decode_range(encoded, 0, INT64_MAX), records, TEST_ROWS);
			parquet_data_free(encoded);
		}
	}
}

static void
test_command_parser(void)
{
	static const char *invalid[] = {
		"", "sync", "sync-", "sync--1", "sync-1-", "sync-1--2",
		"sync-2-1", "sync-+1-2", "sync-1-2x", "sync-1-2-3",
		"syncx-1-2", "asyncx-1-2", "sync-1- 2", "sync-1-2 ",
		"sync-0-9223372036854775808", "sync-0-18446744073709551616",
	};
	NUTS_TRUE(someip_cmd_parser(NULL) == NULL);
	for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		NUTS_TRUE(someip_cmd_parser((void *) invalid[i]) == NULL);
	}
	struct cmd_data *cmd = someip_cmd_parser("sync-0-9223372036854775807");
	NUTS_TRUE(cmd != NULL && cmd->is_sync);
	NUTS_TRUE(cmd->start_key == 0 && cmd->end_key == INT64_MAX);
	NUTS_TRUE(cmd->schema_len == 0 && cmd->schema == NULL);
	nng_free(cmd, sizeof(*cmd));
	cmd = someip_cmd_parser("async-4294967296-4294967297");
	NUTS_TRUE(cmd != NULL && !cmd->is_sync);
	NUTS_TRUE(cmd->start_key == UINT64_C(4294967296));
	NUTS_TRUE(cmd->end_key == UINT64_C(4294967297));
	nng_free(cmd, sizeof(*cmd));
}

static void
test_file_roundtrip(void)
{
	uint8_t frames[TEST_ROWS][TEST_FRAME_MAX];
	someip_record records[TEST_ROWS];
	char path[256];
	const char *tmp = getenv("TMPDIR");
	if (tmp == NULL) tmp = "/tmp";
	NUTS_TRUE(snprintf(path, sizeof(path), "%s/nng-someip-%08x.parquet",
	    tmp, nng_random()) < (int) sizeof(path));
	conf_parquet conf = { 0 };
	conf_parquet_init(&conf);
	conf.comp_type = ZSTD;
	conf.encryption.enable = false;
	for (unsigned mode = 0; mode < 3; mode++) {
		make_records(records, frames, mode == 2, 1);
		parquet_data *encoded = someip_encode_records(records, TEST_ROWS, mode != 0);
		NUTS_TRUE(encoded != NULL);
		NUTS_TRUE(someip_parquet_write_file(&conf, path, encoded, "someip/test") == 0);
		check_text(someip_decode_range(encoded, 0, INT64_MAX), records, TEST_ROWS);
		parquet_data_ret *read = someip_parquet_read_file(&conf, path, 0, INT64_MAX);
		NUTS_ASSERT(read != NULL && read->someip_data != NULL);
		check_equal_batch(batch_of(encoded), read->someip_data);
		check_text(someip_decode(read), records, TEST_ROWS);
		someip_parquet_ret_free(read);
		read = someip_parquet_read_file(&conf, path,
		    records[2].batch_index, records[2].batch_index);
		NUTS_TRUE(read != NULL);
		check_text(someip_decode(read), records + 2, 2);
		someip_parquet_ret_free(read);
		read = someip_parquet_read_file(&conf, path,
		    records[0].batch_index + 1, records[0].batch_index + 1);
		NUTS_TRUE(read != NULL);
		check_text(someip_decode(read), NULL, 0);
		someip_parquet_ret_free(read);
		parquet_data_free(encoded);
	}
	NUTS_TRUE(remove(path) == 0);
}

static void
test_encrypted_file_roundtrip(void)
{
	uint8_t frames[TEST_ROWS][TEST_FRAME_MAX];
	someip_record records[TEST_ROWS];
	char path[256];
	const char *tmp = getenv("TMPDIR");
	if (tmp == NULL) tmp = "/tmp";
	NUTS_TRUE(snprintf(path, sizeof(path), "%s/nng-someip-encrypted-%08x.parquet",
	    tmp, nng_random()) < (int) sizeof(path));
	conf_parquet conf = { 0 };
	conf_parquet_init(&conf);
	conf.comp_type = ZSTD;
	conf.encryption.enable = true;
	conf.encryption.key = "0123456789abcdef";
	conf.encryption.key_id = "someip-test-key";
	/* No wrapped key: the wrong-key check must use the supplied local key. */
	conf.encryption.key_cipher = NULL;
	make_records(records, frames, true, 1);
	parquet_data *encoded = someip_encode_records(records, TEST_ROWS, true);
	NUTS_TRUE(encoded != NULL);
	for (unsigned plain_footer = 0; plain_footer < 2; plain_footer++) {
		conf.encryption.plaintext_footer = plain_footer != 0;
		NUTS_TRUE(someip_parquet_write_file(&conf, path, encoded, "someip/encrypted") == 0);
		parquet_data_ret *read = someip_parquet_read_file(&conf, path, 0, INT64_MAX);
		NUTS_ASSERT(read != NULL && read->someip_data != NULL);
		check_equal_batch(batch_of(encoded), read->someip_data);
		check_text(someip_decode(read), records, TEST_ROWS);
		someip_parquet_ret_free(read);

		conf.encryption.key = "fedcba9876543210";
		read = someip_parquet_read_file(&conf, path,
		    records[2].batch_index, records[2].batch_index);
		NUTS_TRUE(read == NULL);
		conf.encryption.key = "0123456789abcdef";
		read = someip_parquet_read_file(&conf, path,
		    records[2].batch_index, records[2].batch_index);
		NUTS_TRUE(read != NULL);
		check_text(someip_decode(read), records + 2, 2);
		someip_parquet_ret_free(read);
		check_text(someip_decode_range(encoded, 0, INT64_MAX), records, TEST_ROWS);
	}
	parquet_data_free(encoded);
	NUTS_TRUE(remove(path) == 0);
}

NUTS_TESTS = {
	{ "SOME/IP window and record equivalence", test_window_record_equivalence },
	{ "SOME/IP rejects malformed whole input", test_input_rejection },
	{ "SOME/IP integrity and range validation", test_integrity_and_structure },
	{ "SOME/IP strict range commands", test_command_parser },
	{ "SOME/IP five-column file roundtrip", test_file_roundtrip },
	{ "SOME/IP encrypted footer and key roundtrip", test_encrypted_file_roundtrip },
	{ NULL, NULL },
};
