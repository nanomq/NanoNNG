#include "nng/exchange/stream/raw_stream.h"
#include "nng/exchange/stream/stream.h"
#include <nuts.h>
#include <stdlib.h>
#include <string.h>
#if defined(SUPP_PARQUET)
#include "nng/exchange/stream/someip_stream.h"
#endif

void test_raw_stream_register_success(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	stream_sys_fini();
}

void test_raw_cmd_parser_valid_sync(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "sync-100-200";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd != NULL);
	NUTS_TRUE(cmd->is_sync == true);
	NUTS_TRUE(cmd->start_key == 100);
	NUTS_TRUE(cmd->end_key == 200);
	NUTS_TRUE(cmd->schema_len == 2);

	if (cmd) {
		if (cmd->schema) {
			for (uint32_t i = 0; i < cmd->schema_len; i++) {
				if (cmd->schema[i]) {
					nng_free(cmd->schema[i], strlen(cmd->schema[i]) + 1);
				}
			}
			nng_free(cmd->schema, cmd->schema_len * sizeof(char *));
		}
		nng_free(cmd, sizeof(struct cmd_data));
	}

	stream_sys_fini();
}

void test_raw_cmd_parser_valid_async(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "async-500-1000";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd != NULL);
	NUTS_TRUE(cmd->is_sync == false);
	NUTS_TRUE(cmd->start_key == 500);
	NUTS_TRUE(cmd->end_key == 1000);

	if (cmd) {
		if (cmd->schema) {
			for (uint32_t i = 0; i < cmd->schema_len; i++) {
				if (cmd->schema[i]) {
					nng_free(cmd->schema[i], strlen(cmd->schema[i]) + 1);
				}
			}
			nng_free(cmd->schema, cmd->schema_len * sizeof(char *));
		}
		nng_free(cmd, sizeof(struct cmd_data));
	}

	stream_sys_fini();
}

void test_raw_cmd_parser_invalid_format(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "invalid-100-200";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd == NULL);

	stream_sys_fini();
}

void test_raw_cmd_parser_missing_dash(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "sync100200";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd == NULL);

	stream_sys_fini();
}

void test_raw_cmd_parser_non_numeric_keys(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "sync-abc-def";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd == NULL);

	stream_sys_fini();
}

void test_raw_cmd_parser_zero_keys(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "sync-0-0";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd != NULL);
	NUTS_TRUE(cmd->start_key == 0);
	NUTS_TRUE(cmd->end_key == 0);

	if (cmd) {
		if (cmd->schema) {
			for (uint32_t i = 0; i < cmd->schema_len; i++) {
				if (cmd->schema[i]) {
					nng_free(cmd->schema[i], strlen(cmd->schema[i]) + 1);
				}
			}
			nng_free(cmd->schema, cmd->schema_len * sizeof(char *));
		}
		nng_free(cmd, sizeof(struct cmd_data));
	}

	stream_sys_fini();
}

void test_raw_cmd_parser_large_keys(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "async-999999999-1000000000";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd != NULL);
	NUTS_TRUE(cmd->start_key == 999999999);
	NUTS_TRUE(cmd->end_key == 1000000000);

	if (cmd) {
		if (cmd->schema) {
			for (uint32_t i = 0; i < cmd->schema_len; i++) {
				if (cmd->schema[i]) {
					nng_free(cmd->schema[i], strlen(cmd->schema[i]) + 1);
				}
			}
			nng_free(cmd->schema, cmd->schema_len * sizeof(char *));
		}
		nng_free(cmd, sizeof(struct cmd_data));
	}

	stream_sys_fini();
}

void test_raw_encode_null_input(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *result = raw_encode(NULL);
	NUTS_TRUE(result == NULL);

	stream_sys_fini();
}

void test_raw_decode_null_input(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	void *result = raw_decode(NULL);
	NUTS_TRUE(result == NULL);

	stream_sys_fini();
}

static void test_raw_decode_payload_validation(void)
{
	uint8_t first[] = { 'a', 0, 'b' }, second[] = { 'c', 'd' };
	parquet_data_packet a = { .data = first, .size = sizeof(first) };
	parquet_data_packet b = { .data = second, .size = sizeof(second) };
	parquet_data_packet *rows[] = { &a, &b };
	parquet_data_packet **columns[] = { NULL, rows };
	parquet_data_ret result = { .col_len = 1, .row_len = 2,
		.payload_arr = columns + 1 };
	static const uint8_t expected[] = { 'a', 0, 'b', 'c', 'd' };

	/* Legacy raw reader results still concatenate binary payloads. A
	 * missing projected column is skipped consistently in both passes. */
	for (unsigned sparse = 0; sparse < 2; sparse++) {
		result.col_len = sparse ? 2 : 1;
		result.payload_arr = sparse ? columns : columns + 1;
		struct stream_decoded_data *decoded = raw_decode(&result);
		NUTS_ASSERT(decoded != NULL);
		NUTS_TRUE(decoded->len == sizeof(expected));
		NUTS_TRUE(memcmp(decoded->data, expected, sizeof(expected)) == 0);
		stream_decoded_data_free(decoded);
	}
	result.payload_arr = NULL;
	NUTS_TRUE(raw_decode(&result) == NULL);
	result.payload_arr = columns;
	/* Empty cells may legitimately carry a NULL data pointer. */
	a.data = NULL;
	a.size = 0;
	struct stream_decoded_data *decoded = raw_decode(&result);
	NUTS_ASSERT(decoded != NULL);
	NUTS_TRUE(decoded->len == sizeof(second));
	NUTS_TRUE(memcmp(decoded->data, second, sizeof(second)) == 0);
	stream_decoded_data_free(decoded);
	a.size = sizeof(first);
	a.data = NULL;
	NUTS_TRUE(raw_decode(&result) == NULL);
	a.data = first;
	a.size = UINT32_MAX;
	NUTS_TRUE(raw_decode(&result) == NULL);
}

#if defined(SUPP_PARQUET)
static void test_raw_decode_rejects_someip_file(void)
{
	uint8_t frame[16] = { [7] = 8, [12] = 1 };
	someip_record record = { .batch_index = 10, .frame = frame,
		.size = sizeof(frame) };
	parquet_data *encoded = someip_encode_records(&record, 1, true);
	NUTS_ASSERT(encoded != NULL);
	char path[256];
	const char *tmp = getenv("TMPDIR");
	if (tmp == NULL) tmp = "/tmp";
	NUTS_TRUE(snprintf(path, sizeof(path), "%s/nng-raw-someip-%08x.parquet",
	    tmp, nng_random()) < (int) sizeof(path));
	conf_parquet conf = { 0 };
	conf_parquet_init(&conf);
	conf.encryption.enable = false;
	NUTS_PASS(someip_parquet_write_file(&conf, path, encoded, "raw/changed-codec"));
	parquet_data_ret *read = someip_parquet_read_file(&conf, path, 0, INT64_MAX);
	NUTS_ASSERT(read != NULL);
	NUTS_TRUE(read->col_len == 5 && read->someip_data != NULL);
	NUTS_TRUE(read->payload_arr == NULL);
	NUTS_TRUE(raw_decode(read) == NULL);
	/* Rejection borrows the result; its original codec can still use it. */
	struct stream_decoded_data *decoded = someip_decode(read);
	NUTS_ASSERT(decoded != NULL && decoded->len > 0);
	stream_decoded_data_free(decoded);
	someip_parquet_ret_free(read);
	parquet_data_free(encoded);
	NUTS_TRUE(remove(path) == 0);
}
#endif

void test_raw_cmd_parser_single_dash(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "sync-100";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd == NULL);

	stream_sys_fini();
}

void test_raw_cmd_parser_three_dashes(void)
{
	int ret = stream_sys_init();
	NUTS_TRUE(ret == 0);

	const char *input = "sync-100-200-300";
	struct cmd_data *cmd = raw_cmd_parser((void *)input);
	NUTS_TRUE(cmd == NULL);

	stream_sys_fini();
}

NUTS_TESTS = {
	{ "raw stream register success", test_raw_stream_register_success },
	{ "raw cmd parser valid sync", test_raw_cmd_parser_valid_sync },
	{ "raw cmd parser valid async", test_raw_cmd_parser_valid_async },
	{ "raw cmd parser invalid format", test_raw_cmd_parser_invalid_format },
	{ "raw cmd parser missing dash", test_raw_cmd_parser_missing_dash },
	{ "raw cmd parser non numeric keys", test_raw_cmd_parser_non_numeric_keys },
	{ "raw cmd parser zero keys", test_raw_cmd_parser_zero_keys },
	{ "raw cmd parser large keys", test_raw_cmd_parser_large_keys },
	{ "raw encode null input", test_raw_encode_null_input },
	{ "raw decode null input", test_raw_decode_null_input },
	{ "raw decode payload validation", test_raw_decode_payload_validation },
#if defined(SUPP_PARQUET)
	{ "raw decode rejects SOME/IP file", test_raw_decode_rejects_someip_file },
#endif
	{ "raw cmd parser single dash", test_raw_cmd_parser_single_dash },
	{ "raw cmd parser three dashes", test_raw_cmd_parser_three_dashes },
	{ NULL, NULL },
};
