#include "nng/exchange/stream/raw_stream.h"
#include "nng/exchange/stream/stream.h"
#include <nuts.h>
#include <string.h>

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
	{ "raw cmd parser single dash", test_raw_cmd_parser_single_dash },
	{ "raw cmd parser three dashes", test_raw_cmd_parser_three_dashes },
	{ NULL, NULL },
};