// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include <string.h>
#include "nng/exchange/stream/stream.h"
#include "nng/exchange/stream/raw_stream.h"

nng_id_map *stream_node_map = NULL;

int stream_register(char *name,
					uint8_t id,
					void *(*decode)(void *),
					void *(*encode)(void *),
					void *(*cmd_parser)(void *))
{
	stream_node *snode = NULL;

	if (decode == NULL || encode == NULL || cmd_parser == NULL) {
		return NNG_EINVAL;
	}

	snode = nng_id_get(stream_node_map, id);
	if (snode != NULL) {
		return NNG_EEXIST;
	}

	snode = nng_alloc(sizeof(*snode));
	if (snode == NULL) {
		return NNG_ENOMEM;
	}

	snode->name   = name;
	snode->id     = id;
	snode->decode = decode;
	snode->encode = encode;
	snode->cmd_parser = cmd_parser;

	nng_id_set(stream_node_map, id, snode);

	return 0;
}

int stream_unregister(uint8_t id)
{
	stream_node *snode = NULL;
	snode = nng_id_get(stream_node_map, id);
	if (snode == NULL) {
		return NNG_ENOENT;
	}

	nng_id_remove(stream_node_map, id);

	nng_free(snode->name, strlen(snode->name) + 1);
	nng_free(snode, sizeof(*snode));

	return 0;
}

void *stream_decode(uint8_t id, void *buf)
{
	stream_node *snode = NULL;
	snode = nng_id_get(stream_node_map, id);
	if (snode == NULL) {
		return NULL;
	}

	return (snode->decode(buf));
}

void *stream_encode(uint8_t id, void *buf)
{
	stream_node *snode = NULL;
	snode = nng_id_get(stream_node_map, id);
	if (snode == NULL) {
		return NULL;
	}

	return snode->encode(buf);
}

void *stream_cmd_parser(uint8_t id, void *buf)
{
	stream_node *snode = NULL;

	// If input buffer is NULL, parser should return NULL directly.
	// This avoids passing a NULL pointer down to specific cmd parsers
	// (e.g. raw_cmd_parser) which expect a valid string and would
	// otherwise trigger undefined behavior.
	if (buf == NULL) {
		return NULL;
	}

	snode = nng_id_get(stream_node_map, id);
	if (snode == NULL) {
		return NULL;
	}

	return (snode->cmd_parser(buf));
}

#define UNUSED(x) ((void) x)

void stream_node_destory(void *id, void *value)
{
	UNUSED(id);
	stream_node *snode = value;

	nng_free(snode->name, strlen(snode->name) + 1);
	nng_free(snode, sizeof(*snode));

	return;
}

void stream_decoded_data_free(struct stream_decoded_data *data)
{
	if (data != NULL) {
		if (data->data != NULL) {
			nng_free(data->data, data->len);
		}
		nng_free(data, sizeof(*data));
	}

	return;
}

void stream_data_out_free(struct stream_data_out *data)
{

	if (data != NULL) {
		if (data->ts != NULL) {
			nng_free(data->ts, data->col_len * sizeof(uint64_t));
		}
		if (data->schema != NULL) {
			for (uint32_t i = 0; i < data->col_len; i++) {
				nng_free(data->schema[i], strlen(data->schema[i]) + 1);
			}
			nng_free(data->schema, data->col_len * sizeof(char *));
		}
		if (data->payload_arr != NULL) {
			for (uint32_t i = 0; i < data->col_len - 1; i++) {
				if (data->payload_arr[i] != NULL) {
					for (uint32_t j = 0; j < data->row_len; j++) {
						nng_free(data->payload_arr[i][j], sizeof(parquet_data_packet));
					}
					nng_free(data->payload_arr[i], data->row_len * sizeof(char *));
				}
			}
			nng_free(data->payload_arr, data->col_len * sizeof(char **));
		}
		nng_free(data, sizeof(*data));
	}

	return;
}

void stream_data_in_free(struct stream_data_in *sdata)
{
	if (sdata == NULL) {
		return;
	}

	if (sdata->datas != NULL) {
		nng_free(sdata->datas, sizeof(void *) * sdata->len);
	}

	if (sdata->keys != NULL) {
		nng_free(sdata->keys, sizeof(uint64_t) * sdata->len);
	}

	if (sdata->lens != NULL) {
		nng_free(sdata->lens, sizeof(uint32_t) * sdata->len);
	}

	nng_free(sdata, sizeof(struct stream_data_in));

	return;
}

int stream_sys_init(void)
{
	int ret = 0;

	// Allow stream_sys_init to be called multiple times safely.
	// If already initialized, just return success.
	if (stream_node_map != NULL) {
		return 0;
	}

	ret = nng_id_map_alloc(&stream_node_map, 0, 0, false);
	if (ret != 0) {
		return ret;
	}

	raw_stream_register();

	return 0;
}

void stream_sys_fini(void)
{
	// Make stream_sys_fini idempotent so that double fini
	// does not lead to double-free on the id map.
	if (stream_node_map == NULL) {
		return;
	}

	nng_id_map_foreach(stream_node_map, stream_node_destory);
	nng_id_map_free(stream_node_map);
	stream_node_map = NULL;

	return;
}
#ifndef PARQUET_H
parquet_data *parquet_data_alloc(char **schema, parquet_data_packet ***payload_arr,
    uint64_t *ts, uint32_t col_len, uint32_t row_len)
{
	if (payload_arr == NULL || schema == NULL || col_len == 0 ||
	    row_len == 0) {
		log_error("payload || schema should not be NULL, col || row "
		          "len should't == 0");
	}
	parquet_data *data = nng_alloc(sizeof(parquet_data));
	if (data == NULL) {
		return NULL; // Memory allocation failed
	}
	data->ts          = ts;
	data->col_len     = col_len + 1;
	data->row_len     = row_len;
	data->schema      = schema;
	data->payload_arr = payload_arr;
	return data;
}

void parquet_data_free(parquet_data *data)
{
	if (data) {
		for (uint32_t c = 0; c < data->col_len - 1; c++) {
			if (data->schema[c] != NULL) {
				nng_free(data->schema[c], strlen(data->schema[c]));
			}
			for (uint32_t r = 0; r < data->row_len; r++) {
				parquet_data_packet *payload =
				    data->payload_arr[c][r];
				if (payload != NULL) {
					if (payload->data != NULL && payload->size > 0) {
						nng_free(payload->data, payload->size);
					}
					nng_free(payload, sizeof(parquet_data_packet));
				}
			}
			if (data->payload_arr[c] != NULL) {
				nng_free(data->payload_arr[c],
				    data->row_len * sizeof(parquet_data_packet *));
			}
		}

		if (data->schema[data->col_len - 1] != NULL) {
			nng_free(data->schema[data->col_len - 1],
			    strlen(data->schema[data->col_len - 1]));
		}
		if (data->schema != NULL) {
			nng_free(data->schema, data->col_len * sizeof(char *));
		}
		if (data->ts != NULL) {
			nng_free(data->ts, data->row_len * sizeof(uint64_t));
		}
		if (data->payload_arr != NULL) {
			nng_free(data->payload_arr,
			    data->col_len * sizeof(parquet_data_packet **));
		}
		nng_free(data, sizeof(parquet_data));
	}
	return;
}
#endif
