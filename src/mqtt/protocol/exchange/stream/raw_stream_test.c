#include "nng/exchange/stream/raw_stream.h"
#include "nng/exchange/stream/stream.h"
#include "nng/mqtt/mqtt_client.h"
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

static nng_msg *
broker_payload_msg(const uint8_t *payload, uint32_t size, uint64_t key)
{
	static const uint8_t topic[] = { 0, 3, 't', '/', 'x' };
	nng_msg *msg = NULL;
	NUTS_ASSERT(nng_msg_alloc(&msg, sizeof(topic) + size) == 0);
	memcpy(nng_msg_body(msg), topic, sizeof(topic));
	if (size > 0) {
		memcpy((uint8_t *) nng_msg_body(msg) + sizeof(topic), payload, size);
	}
	nng_msg_set_payload_ptr(msg, (uint8_t *) nng_msg_body(msg) + sizeof(topic));
	/* ringBuffer_get_msgs uses proto_data for an integer exchange key. */
	nng_msg_set_proto_data(msg, NULL, (void *) (uintptr_t) key);
	return msg;
}

static void
test_payload_span_broker_and_client(void)
{
	static uint8_t bytes[] = { 0, 255, 1, 2, 3 };
	uint32_t size = UINT32_MAX;
	nng_msg *broker = broker_payload_msg(bytes, sizeof(bytes), UINT64_C(0x123456789));
	uint8_t *data = stream_msg_payload(broker, &size);
	NUTS_ASSERT(data != NULL);
	NUTS_TRUE(size == sizeof(bytes) && memcmp(data, bytes, size) == 0);
	NUTS_TRUE(stream_msg_payload(broker, NULL) == data);
	/* The public getter also supports broker spans without casting the key. */
	NUTS_TRUE(nng_mqtt_msg_get_publish_payload(broker, &size) == data);
	NUTS_TRUE(size == sizeof(bytes));

	uint8_t *valid = nng_msg_payload_ptr(broker);
	nng_msg_set_payload_ptr(broker,
	    (uint8_t *) ((uintptr_t) nng_msg_body(broker) + nng_msg_len(broker) + 1));
	size = 99;
	NUTS_TRUE(stream_msg_payload(broker, &size) == NULL && size == 0);
	nng_msg_set_payload_ptr(broker, (uint8_t *) (uintptr_t) 1);
	NUTS_TRUE(stream_msg_payload(broker, &size) == NULL && size == 0);
	nng_msg_set_payload_ptr(broker, valid);
	NUTS_TRUE(stream_msg_payload(broker, &size) == valid && size == sizeof(bytes));
	nng_msg_free(broker);

	nng_msg *client = NULL;
	NUTS_ASSERT(nng_mqtt_msg_alloc(&client, 0) == 0);
	nng_mqtt_msg_set_packet_type(client, NNG_MQTT_PUBLISH);
	NUTS_ASSERT(nng_mqtt_msg_set_publish_topic(client, "client/test") == 0);
	NUTS_ASSERT(nng_mqtt_msg_set_publish_payload(client, bytes, sizeof(bytes)) == 0);
	NUTS_ASSERT(nng_mqtt_msg_encode(client) == 0);
	data = stream_msg_payload(client, &size);
	NUTS_ASSERT(data != NULL);
	NUTS_TRUE(size == sizeof(bytes) && memcmp(data, bytes, size) == 0);
	NUTS_TRUE(nng_mqtt_msg_get_publish_payload(client, NULL) == data);
	/* Typed payload wins even when its body has a separate payload pointer. */
	NUTS_ASSERT(nng_msg_append(client, "unrelated", 9) == 0);
	nng_msg_set_payload_ptr(client, nng_msg_body(client));
	NUTS_TRUE(stream_msg_payload(client, &size) == data && size == sizeof(bytes));
	nng_msg_free(client);

	NUTS_ASSERT(nng_mqtt_msg_alloc(&client, 0) == 0);
	nng_mqtt_msg_set_packet_type(client, NNG_MQTT_PUBLISH);
	NUTS_ASSERT(nng_mqtt_msg_set_publish_topic(client, "client/empty") == 0);
	NUTS_ASSERT(nng_mqtt_msg_encode(client) == 0);
	NUTS_TRUE(stream_msg_payload(client, &size) != NULL && size == 0);
	nng_msg_free(client);
	broker = broker_payload_msg(NULL, 0, 42);
	NUTS_TRUE(stream_msg_payload(broker, &size) != NULL && size == 0);
	nng_msg_free(broker);

	NUTS_ASSERT(nng_msg_alloc(&broker, 0) == 0);
	nng_msg_set_proto_data(broker, NULL, (void *) (uintptr_t) 1234);
	NUTS_TRUE(stream_msg_payload(broker, &size) == NULL && size == 0);
	nng_msg_free(broker);
	NUTS_TRUE(stream_msg_payload(NULL, &size) == NULL && size == 0);
	NUTS_TRUE(nng_mqtt_msg_get_publish_payload(NULL, &size) == NULL && size == 0);
}

static void
test_raw_encode_first_full_broker_batch(void)
{
	enum { ROWS = 1200 };
	nng_msg *messages[ROWS];
	uint64_t keys[ROWS];
	for (uint32_t i = 0; i < ROWS; i++) {
		uint8_t bytes[] = { (uint8_t) (i >> 8), (uint8_t) i, 0, 255 };
		keys[i] = 1000000 + i;
		messages[i] = broker_payload_msg(bytes, sizeof(bytes), keys[i]);
	}
	struct stream_data_in input = { (void **) messages, keys, ROWS };
	parquet_data *batch = raw_encode(&input);
	NUTS_ASSERT(batch != NULL);
	NUTS_TRUE(batch->row_len == ROWS && batch->col_len == 2);
	NUTS_TRUE(strcmp(batch->schema[0], "ts") == 0);
	NUTS_TRUE(strcmp(batch->schema[1], "data") == 0);
	for (uint32_t i = 0; i < ROWS; i++) {
		uint8_t bytes[] = { (uint8_t) (i >> 8), (uint8_t) i, 0, 255 };
		NUTS_TRUE(batch->ts[i] == keys[i]);
		NUTS_ASSERT(batch->payload_arr[0][i] != NULL);
		NUTS_TRUE(batch->payload_arr[0][i]->size == sizeof(bytes));
		NUTS_TRUE(memcmp(batch->payload_arr[0][i]->data, bytes, sizeof(bytes)) == 0);
	}
	/* Copies own the archived bytes and do not depend on MQTT message lifetime. */
	memset(nng_msg_payload_ptr(messages[0]), 0, 4);
	NUTS_TRUE(batch->payload_arr[0][0]->data[3] == 255);
	parquet_data_free(batch);

	/* Reject a malformed later row and release all partially copied rows. */
	nng_msg *saved = messages[ROWS - 1];
	messages[ROWS - 1] = NULL;
	NUTS_TRUE(raw_encode(&input) == NULL);
	messages[ROWS - 1] = saved;
	input.keys = NULL;
	NUTS_TRUE(raw_encode(&input) == NULL);
	for (uint32_t i = 0; i < ROWS; i++) {
		nng_msg_free(messages[i]);
	}
}

static void
test_raw_encode_client_and_empty_payloads(void)
{
	uint8_t bytes[] = { 4, 3, 2, 1 };
	nng_msg *messages[2];
	uint64_t keys[] = { 10, 11 };
	NUTS_ASSERT(nng_mqtt_msg_alloc(&messages[0], 0) == 0);
	nng_mqtt_msg_set_packet_type(messages[0], NNG_MQTT_PUBLISH);
	NUTS_ASSERT(nng_mqtt_msg_set_publish_topic(messages[0], "client/raw") == 0);
	NUTS_ASSERT(nng_mqtt_msg_set_publish_payload(messages[0], bytes, sizeof(bytes)) == 0);
	NUTS_ASSERT(nng_mqtt_msg_encode(messages[0]) == 0);
	messages[1] = broker_payload_msg(NULL, 0, keys[1]);
	struct stream_data_in input = { (void **) messages, keys, 2 };
	parquet_data *batch = raw_encode(&input);
	NUTS_ASSERT(batch != NULL);
	NUTS_TRUE(batch->payload_arr[0][0]->size == sizeof(bytes));
	NUTS_TRUE(memcmp(batch->payload_arr[0][0]->data, bytes, sizeof(bytes)) == 0);
	NUTS_ASSERT(batch->payload_arr[0][1] != NULL);
	NUTS_TRUE(batch->payload_arr[0][1]->size == 0);
	parquet_data_free(batch);
	nng_msg_free(messages[0]);
	nng_msg_free(messages[1]);
}

NUTS_TESTS = {
	{ "payload spans for broker and client", test_payload_span_broker_and_client },
	{ "raw encode first full broker batch", test_raw_encode_first_full_broker_batch },
	{ "raw encode client and empty payloads", test_raw_encode_client_and_empty_payloads },
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
