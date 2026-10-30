// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include <string.h>
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/exchange/stream/stream.h"
#include "nng/exchange/stream/raw_stream.h"
#include "supplemental/mqtt/mqtt_msg.h"

/* Keep the lock across lookup and reference acquisition, never across a
 * codec callback. Unregister waits for active callbacks before dlclose. */
static nni_mtx stream_lock = NNI_MTX_INITIALIZER;
static nni_cv stream_cv = NNI_CV_INITIALIZER(&stream_lock);
static stream_node *stream_nodes[256];
static uint32_t stream_active[256];
static bool stream_retiring[256];
static bool stream_ready;
static bool stream_closing;

static int
stream_parse_u64(const char **cursor, uint64_t *value)
{
	const char *p = *cursor;
	uint64_t n = 0;
	if (*p < '0' || *p > '9') return NNG_EINVAL;
	while (*p >= '0' && *p <= '9') {
		unsigned digit = (unsigned) (*p++ - '0');
		if (n > (UINT64_MAX - digit) / 10) return NNG_EINVAL;
		n = n * 10 + digit;
	}
	*cursor = p;
	*value = n;
	return 0;
}

int
stream_parse_cmd_range(const char *input, bool *sync, uint64_t *start,
    uint64_t *end, const char **schema)
{
	if (!input || !sync || !start || !end || !schema) return NNG_EINVAL;
	const char *p;
	if (strncmp(input, "sync-", 5) == 0) {
		*sync = true;
		p = input + 5;
	} else if (strncmp(input, "async-", 6) == 0) {
		*sync = false;
		p = input + 6;
	} else return NNG_EINVAL;
	if (stream_parse_u64(&p, start) != 0 || *p++ != '-' ||
	    stream_parse_u64(&p, end) != 0 || *start > *end ||
	    (*p != '\0' && *p != '-')) return NNG_EINVAL;
	*schema = *p == '-' ? p + 1 : NULL;
	return 0;
}

static int
stream_msg_try_decode(nng_msg *m)
{
	uint8_t cmd = nng_msg_get_cmd_type(m);
	if (nng_msg_header_len(m) < 2 ||
	    (cmd != CMD_PUBLISH && cmd != CMD_PUBLISH_V5 &&
	        cmd != CMD_PUBLISH_V5_RECV)) {
		return NNG_EINVAL;
	}
	if (nni_msg_get_proto_data(m) == NULL &&
	    nng_mqtt_msg_proto_data_alloc(m) != 0) {
		return NNG_ENOMEM;
	}
	return cmd == CMD_PUBLISH ? nng_mqtt_msg_decode(m) :
	                           nng_mqttv5_msg_decode(m);
}

uint8_t *
stream_msg_payload(void *msg, uint32_t *out_len)
{
	nng_msg *m = msg;
	uint8_t *payload;
	uint32_t plen = 0;
	if (out_len != NULL) {
		*out_len = 0;
	}
	if (m == NULL) {
		return NULL;
	}
	payload = nng_msg_payload_ptr(m);
	if (payload == NULL) {
		if (stream_msg_try_decode(m) != 0) {
			return NULL;
		}
		payload = nng_mqtt_msg_get_publish_payload(m, &plen);
	}
	/* Validate cached and decoded pointers before subtracting. Empty payloads
	 * may point exactly one byte past the body. */
	uintptr_t body = (uintptr_t) nng_msg_body(m);
	uintptr_t ptr = (uintptr_t) payload;
	size_t len = nng_msg_len(m);
	if (payload == NULL || ptr < body || ptr - body > len ||
	    len - (ptr - body) > UINT32_MAX) {
		return NULL;
	}
	plen = (uint32_t) (len - (ptr - body));
	nng_msg_set_payload_ptr(m, payload);
	if (out_len != NULL) {
		*out_len = plen;
	}
	return payload;
}

static int
stream_register_locked(char *name, uint8_t id, void *(*decode)(void *),
    void *(*encode)(void *), void *(*cmd_parser)(void *))
{
	if (!stream_ready || stream_closing) {
		return NNG_ECLOSED;
	}
	if (stream_nodes[id] != NULL || stream_retiring[id]) {
		return NNG_EEXIST;
	}
	stream_node *node = nng_alloc(sizeof(*node));
	if (node == NULL) {
		return NNG_ENOMEM;
	}
	*node = (stream_node) { name, id, decode, encode, cmd_parser };
	stream_nodes[id] = node;
	return 0;
}

int
stream_register(char *name, uint8_t id, void *(*decode)(void *),
    void *(*encode)(void *), void *(*cmd_parser)(void *))
{
	if (name == NULL || decode == NULL || encode == NULL || cmd_parser == NULL) {
		return NNG_EINVAL;
	}
	nni_mtx_lock(&stream_lock);
	int rv = stream_register_locked(name, id, decode, encode, cmd_parser);
	nni_mtx_unlock(&stream_lock);
	return rv;
}

static void
stream_node_free(stream_node *node)
{
	nng_free(node->name, strlen(node->name) + 1);
	nng_free(node, sizeof(*node));
}

int
stream_unregister(uint8_t id)
{
	nni_mtx_lock(&stream_lock);
	/* Another remover/finalizer may already own this entry. Wait for it,
	 * so even an ENOENT result guarantees its callbacks have drained. */
	if (stream_closing || stream_retiring[id]) {
		while (stream_closing || stream_retiring[id]) nni_cv_wait(&stream_cv);
		nni_mtx_unlock(&stream_lock);
		return NNG_ENOENT;
	}
	stream_node *node = stream_nodes[id];
	if (node == NULL) {
		nni_mtx_unlock(&stream_lock);
		return NNG_ENOENT;
	}
	stream_retiring[id] = true;
	while (stream_active[id] != 0) {
		nni_cv_wait(&stream_cv);
	}
	stream_nodes[id] = NULL;
	stream_node_free(node);
	stream_retiring[id] = false;
	nni_cv_wake(&stream_cv);
	nni_mtx_unlock(&stream_lock);
	return 0;
}

bool
stream_is_registered(uint8_t id)
{
	nni_mtx_lock(&stream_lock);
	bool exists = stream_ready && !stream_closing && !stream_retiring[id] &&
	    stream_nodes[id] != NULL;
	nni_mtx_unlock(&stream_lock);
	return exists;
}

static void *
stream_call(uint8_t id, void *buf, unsigned op)
{
	nni_mtx_lock(&stream_lock);
	stream_node *node = stream_nodes[id];
	if (node == NULL || stream_closing || stream_retiring[id]) {
		nni_mtx_unlock(&stream_lock);
		return NULL;
	}
	stream_active[id]++;
	void *(*fn)(void *) = op == 0 ? node->decode :
	    op == 1 ? node->encode : node->cmd_parser;
	nni_mtx_unlock(&stream_lock);
	void *result = fn(buf);
	nni_mtx_lock(&stream_lock);
	if (--stream_active[id] == 0) {
		nni_cv_wake(&stream_cv);
	}
	nni_mtx_unlock(&stream_lock);
	return result;
}

void *stream_decode(uint8_t id, void *buf) { return stream_call(id, buf, 0); }
void *stream_encode(uint8_t id, void *buf) { return stream_call(id, buf, 1); }
void *stream_cmd_parser(uint8_t id, void *buf)
{
	return buf == NULL ? NULL : stream_call(id, buf, 2);
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
	if (data == NULL) return;
	if (data->payload_arr != NULL) {
		for (uint32_t c = 0; c < data->col_len; c++) {
			if (data->payload_arr[c] == NULL) continue;
			for (uint32_t r = 0; r < data->row_len; r++) {
				parquet_data_packet *cell = data->payload_arr[c][r];
				if (cell == NULL) continue;
				nng_free(cell->data, cell->size);
				nng_free(cell, sizeof(*cell));
			}
			nng_free(data->payload_arr[c],
			    data->row_len * sizeof(parquet_data_packet *));
		}
		nng_free(data->payload_arr,
		    data->col_len * sizeof(parquet_data_packet **));
	}
	if (data->schema != NULL) {
		for (uint32_t c = 0; c <= data->col_len; c++) {
			nng_strfree(data->schema[c]);
		}
		nng_free(data->schema, (data->col_len + 1) * sizeof(char *));
	}
	nng_free(data->ts, data->row_len * sizeof(uint64_t));
	nng_free(data, sizeof(*data));
}

void stream_data_in_free(struct stream_data_in *sdata)
{
	if (sdata == NULL) {
		return;
	}

	/* msgs[] holds borrowed nng_msg* pointers; do not free the messages. */
	if (sdata->msgs != NULL) {
		nng_free(sdata->msgs, sizeof(void *) * sdata->len);
	}

	if (sdata->keys != NULL) {
		nng_free(sdata->keys, sizeof(uint64_t) * sdata->len);
	}

	nng_free(sdata, sizeof(struct stream_data_in));

	return;
}

int stream_sys_init(void)
{
	nni_mtx_lock(&stream_lock);
	while (stream_closing) {
		nni_cv_wait(&stream_cv);
	}
	if (stream_ready) {
		nni_mtx_unlock(&stream_lock);
		return 0;
	}
	char *name = nng_strdup(RAW_STREAM_NAME);
	if (name == NULL) {
		nni_mtx_unlock(&stream_lock);
		return NNG_ENOMEM;
	}
	stream_ready = true;
	int rv = stream_register_locked(name, RAW_STREAM_ID, raw_decode,
	    raw_encode, raw_cmd_parser);
	if (rv != 0) {
		stream_ready = false;
		nng_strfree(name);
	}
	nni_mtx_unlock(&stream_lock);
	return rv;
}

void stream_sys_fini(void)
{
	nni_mtx_lock(&stream_lock);
	while (stream_closing) {
		nni_cv_wait(&stream_cv);
	}
	stream_closing = true;
	for (unsigned id = 0; id < 256; id++) {
		while (stream_active[id] != 0 || stream_retiring[id]) {
			nni_cv_wait(&stream_cv);
		}
		if (stream_nodes[id] != NULL) {
			stream_node_free(stream_nodes[id]);
			stream_nodes[id] = NULL;
		}
	}
	stream_ready = false;
	stream_closing = false;
	nni_cv_wake(&stream_cv);
	nni_mtx_unlock(&stream_lock);
}

#ifndef PARQUET_H
parquet_data *parquet_data_alloc(char **schema, parquet_data_packet ***payload_arr,
    uint64_t *ts, uint32_t col_len, uint32_t row_len)
{
	if (payload_arr == NULL || schema == NULL || ts == NULL ||
	    col_len == 0 || col_len == UINT32_MAX || row_len == 0) {
		log_error("payload || schema should not be NULL, col || row "
		          "len should't == 0");
		return NULL;
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

int
parquet_data_view(const parquet_data *data, parquet_data_ret *view)
{
	if (data == NULL || view == NULL || data->col_len < 2 ||
	    data->schema == NULL || data->payload_arr == NULL || data->ts == NULL) {
		return NNG_EINVAL;
	}
	view->col_len = data->col_len - 1;
	view->row_len = data->row_len;
	view->ts = data->ts;
	view->schema = data->schema + 1;
	view->payload_arr = data->payload_arr;
	return 0;
}

void
parquet_data_free(parquet_data *data)
{
	if (data == NULL) {
		return;
	}
	uint32_t cols = data->col_len > 0 ? data->col_len - 1 : 0;
	if (data->payload_arr != NULL) {
		for (uint32_t c = 0; c < cols; c++) {
			if (data->payload_arr[c] == NULL) {
				continue;
			}
			for (uint32_t r = 0; r < data->row_len; r++) {
				parquet_data_packet *cell = data->payload_arr[c][r];
				if (cell != NULL) {
					if (cell->data != NULL) {
						nng_free(cell->data, cell->size);
					}
					nng_free(cell, sizeof(*cell));
				}
			}
			nng_free(data->payload_arr[c],
			    data->row_len * sizeof(parquet_data_packet *));
		}
		nng_free(data->payload_arr, cols * sizeof(parquet_data_packet **));
	}
	if (data->schema != NULL) {
		for (uint32_t c = 0; c < data->col_len; c++) {
			if (data->schema[c] != NULL) {
				nng_strfree(data->schema[c]);
			}
		}
		nng_free(data->schema, data->col_len * sizeof(char *));
	}
	if (data->ts != NULL) {
		nng_free(data->ts, data->row_len * sizeof(uint64_t));
	}
	nng_free(data, sizeof(*data));
}

#endif
