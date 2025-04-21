// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include <stdlib.h>
#include <string.h>
#include "nng/exchange/stream/raw_stream.h"
#if defined(SUPP_PARQUET)
#include "nng/supplemental/nanolib/parquet.h"
#endif

static char **raw_schema_init()
{
	char **schema = nng_alloc(2 * sizeof(char *));
	if (schema == NULL) {
		return NULL;
	}
	schema[0] = nng_alloc(strlen("ts") + 1);
	if (schema[0] == NULL) {
		nng_free(schema, 2 * sizeof(char *));
		return NULL;
	}
	strcpy(schema[0], "ts");
	schema[1] = nng_alloc(strlen("data") + 1);
	if (schema[1] == NULL) {
		nng_free(schema[0], strlen("ts") + 1);
		nng_free(schema, 2 * sizeof(char *));
		return NULL;
	}
	strcpy(schema[1], "data");

	return schema;
}

static void output_stream_free(struct stream_data_out *output_stream)
{
	if (output_stream == NULL) {
		return;
	}

	if (output_stream->schema != NULL) {
		for (uint32_t i = 0; i < output_stream->col_len; i++) {
			if (output_stream->schema[i] != NULL) {
				nng_free(output_stream->schema[i], strlen(output_stream->schema[i]) + 1);
			}
		}
		nng_free(output_stream->schema, output_stream->col_len * sizeof(char *));
	}

	if (output_stream->payload_arr != NULL) {
		for (uint32_t i = 0; i < output_stream->row_len; i++) {
			if (output_stream->payload_arr[i] != NULL) {
				for (uint32_t j = 0; j < output_stream->col_len; j++) {
					if (output_stream->payload_arr[i][j] != NULL) {
						nng_free(output_stream->payload_arr[i][j], sizeof(output_stream->payload_arr[i][j]));
					}
				}
				nng_free(output_stream->payload_arr[i], sizeof(parquet_data_packet) * output_stream->col_len);
			}
		}
		nng_free(output_stream->payload_arr, sizeof(parquet_data_packet *) * output_stream->row_len * output_stream->col_len);
	}

	nng_free(output_stream, sizeof(struct stream_data_out));

	return;
}

static struct stream_data_out *output_stream_init(void *data)
{
	struct stream_data_out *output_stream = NULL;
	struct stream_data_in *input_stream = NULL;

	input_stream = (struct stream_data_in *)data;
	if (input_stream == NULL || input_stream->len == 0) {
		return NULL;
	}

	output_stream = nng_alloc(sizeof(struct stream_data_out));
	if (output_stream == NULL) {
		return NULL;
	}
	output_stream->schema = NULL;
	output_stream->payload_arr = NULL;

	output_stream->col_len = 1;
	output_stream->row_len = input_stream->len;
	output_stream->schema = raw_schema_init();
	if (output_stream->schema == NULL) {
		output_stream_free(output_stream);
		return NULL;
	}
	output_stream->payload_arr = nng_alloc(sizeof(parquet_data_packet **) * output_stream->row_len);
	if (output_stream->payload_arr == NULL) {
		output_stream_free(output_stream);
		return NULL;
	}

	output_stream->ts = nng_alloc(sizeof(uint64_t) * output_stream->row_len);
	if (output_stream->ts == NULL) {
		output_stream_free(output_stream);
		return NULL;
	}
	for (uint32_t i = 0; i < output_stream->row_len; i++) {
		output_stream->ts[i] = input_stream->keys[i];
	}

	output_stream->payload_arr[0] = nng_alloc(sizeof(parquet_data_packet *) * output_stream->row_len);
	if (output_stream->payload_arr[0] == NULL) {
		output_stream_free(output_stream);
		return NULL;
	}

	for (uint32_t i = 0; i < output_stream->row_len; i++) {
		output_stream->payload_arr[0][i] = NULL;
		output_stream->payload_arr[0][i] = nng_alloc(sizeof(parquet_data_packet) * output_stream->col_len);
		if (output_stream->payload_arr[0][i] == NULL) {
			output_stream_free(output_stream);
			return NULL;
		}
		output_stream->payload_arr[0][i]->size = input_stream->lens[i];
		output_stream->payload_arr[0][i]->data = input_stream->datas[i];
	}

	void *encoded_stream_data = parquet_data_alloc(output_stream->schema,
												   output_stream->payload_arr,
												   output_stream->ts,
												   output_stream->col_len,
												   output_stream->row_len);
	if (encoded_stream_data == NULL) {
		log_error("parquet_data_alloc failed\n");
	}

	nng_free(output_stream, sizeof(struct stream_data_out));

	return encoded_stream_data;
}

static struct stream_decoded_data *raw_stream_decode(struct parquet_data_ret *parquet_data)
{
	struct stream_decoded_data *decoded_data = NULL;

	decoded_data = nng_alloc(sizeof(struct stream_decoded_data));
	if (decoded_data == NULL) {
		log_warn("decoded_data is NULL");
		return NULL;
	}

	decoded_data->data = NULL;
	decoded_data->len = 0;

	for (uint32_t i = 0; i < parquet_data->col_len; i++) {
		if (parquet_data->payload_arr[i] == NULL) {
			log_warn("parquet_data->payload_arr[%d] is NULL", i);
			continue;
		}

		for (uint32_t j = 0; j < parquet_data->row_len; j++) {
			if (parquet_data->payload_arr[i][j] == NULL || parquet_data->payload_arr[i][j]->size == 0) {
				if (parquet_data->payload_arr[i][j] == NULL)
					log_warn("parquet_data->payload_arr[%d][%d] is NULL", i, j);
				else
					log_warn("parquet_data->payload_arr[%d][%d] size: %d", i, j, parquet_data->payload_arr[i][j]->size);

				continue;
			}
			decoded_data->len += parquet_data->payload_arr[i][j]->size;
		}
	}

	if (decoded_data->len == 0) {
		nng_free(decoded_data, sizeof(struct stream_decoded_data));
		log_warn("decoded_data len is 0");
		return NULL;
	}

	decoded_data->data = nng_alloc(decoded_data->len);
	if (decoded_data->data == NULL) {
		log_warn("decoded_data data is null");
		return NULL;
	}

	uint32_t decoded_data_index = 0;
	for (uint32_t i = 0; i < parquet_data->col_len; i++) {
		for (uint32_t j = 0; j < parquet_data->row_len; j++) {
			if (parquet_data->payload_arr[i][j] == NULL) {
				log_warn("parquet_data->payload_arr[%d][%d] is NULL", i, j);
				continue;
			}
			if (parquet_data->payload_arr[i][j]->size == 0) {
				log_warn("parquet_data->payload_arr[%d][%d] size is 0", i, j);
				continue;
			}
			memcpy((uint8_t *)decoded_data->data + decoded_data_index, parquet_data->payload_arr[i][j]->data, parquet_data->payload_arr[i][j]->size);
			decoded_data_index += parquet_data->payload_arr[i][j]->size;
		}
	}

	return decoded_data;
}

void *raw_decode(void *data)
{
	struct parquet_data_ret *parquet_data = (struct parquet_data_ret *)data;
	if (parquet_data == NULL) {
		log_warn("parquet_data is NULL");
		return NULL;
	}

	struct stream_decoded_data *decoded_data = NULL;

	decoded_data = raw_stream_decode(parquet_data);

	return decoded_data;
}

void *raw_encode(void *data)
{
	struct stream_data_out *output_stream = NULL;
	if (data == NULL) {
		return NULL;
	}

	output_stream = output_stream_init(data);
	if (output_stream == NULL) {
		return NULL;
	}

	return output_stream;
}

static int checkInput(const char *input,
					  uint32_t *start_key_index,
					  uint32_t *end_key_index)
{
	int count = 0;

	*start_key_index = 0;
	*end_key_index = 0;

	if (strncmp(input, "sync", 4) != 0 && strncmp(input, "async", 5) != 0) {
		log_error("Error: Invalid input format\n");
		return -1;
	}

	for (unsigned int i = 0; i < strlen(input); i++) {
		if (input[i] == '-') {
			if (count == 0) {
				*start_key_index = i + 1;
			} else if (count == 1) {
				*end_key_index = i + 1;
			}
			count++;
		}
	}

	if (count != 2) {
		log_error("Error: Invalid input format\n");
		return -1;
	}

	for (unsigned int i = *start_key_index; i < *end_key_index - 1; i++) {
		if (input[i] < '0' || input[i] > '9') {
			log_error("Error: Invalid input format\n");
			return -1;
		}
	}

	for (unsigned int i = *end_key_index; i < strlen(input); i++) {
		if (input[i] < '0' || input[i] > '9') {
			log_error("Error: Invalid input format\n");
			return -1;
		}
	}

	return 0;
}

static struct cmd_data *parse_input_cmd(const char *input)
{
	struct cmd_data *cmd_data = NULL;
	uint32_t start_key_index = 0;
	uint32_t end_key_index = 0;

	cmd_data = (struct cmd_data *)nng_alloc(sizeof(struct cmd_data));
	if (cmd_data == NULL) {
		return NULL;
	}

	if (checkInput(input, &start_key_index, &end_key_index) != 0) {
		log_error("checkInput failed\n");
		nng_free(cmd_data, sizeof(struct cmd_data));
		return NULL;
	}

	if (strncmp(input, "sync", 4) == 0) {
		cmd_data->is_sync = true;
	} else if (strncmp(input, "async", 5) == 0) {
		cmd_data->is_sync = false;
	} else {
		log_error("Error: Invalid input format\n");
		nng_free(cmd_data, sizeof(struct cmd_data));
		return NULL;
	}

	cmd_data->start_key = (uint64_t)atoll(input + start_key_index);
	cmd_data->end_key = (uint64_t)atoll(input + end_key_index);

	cmd_data->schema_len = 2;
	cmd_data->schema = nng_alloc(cmd_data->schema_len * sizeof(char *));
	if (cmd_data->schema == NULL) {
		nng_free(cmd_data, sizeof(struct cmd_data));
		return NULL;
	}
	cmd_data->schema[0] = nng_alloc(strlen("ts") + 1);
	if (cmd_data->schema[0] == NULL) {
		nng_free(cmd_data->schema, cmd_data->schema_len * sizeof(char *));
		nng_free(cmd_data, sizeof(struct cmd_data));
		return NULL;
	}
	strcpy(cmd_data->schema[0], "ts");
	cmd_data->schema[1] = nng_alloc(strlen("data") + 1);
	if (cmd_data->schema[1] == NULL) {
		nng_free(cmd_data->schema[0], strlen("ts") + 1);
		nng_free(cmd_data->schema, cmd_data->schema_len * sizeof(char *));
		nng_free(cmd_data, sizeof(struct cmd_data));
		return NULL;
	}
	strcpy(cmd_data->schema[1], "data");


	log_info("start_key: %ld end_key: %ld", cmd_data->start_key, cmd_data->end_key);

	return cmd_data;
}

void *raw_cmd_parser(void *data)
{
	struct cmd_data *cmd_data = NULL;

	cmd_data = parse_input_cmd(data);

	return cmd_data;
}

int raw_stream_register()
{
	int ret = 0;
	char *name = NULL;
	name = nng_alloc(strlen(RAW_STREAM_NAME) + 1);
	if (name == NULL) {
		return NNG_ENOMEM;
	}

	strcpy(name, RAW_STREAM_NAME);

	ret = stream_register(name, RAW_STREAM_ID, raw_decode, raw_encode, raw_cmd_parser);
	if (ret != 0) {
		nng_free(name, strlen(name) + 1);
		return ret;
	}

	return 0;
}
