// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
#ifndef STREAM_H
#define STREAM_H
#include "nng/nng.h"
#include "nng/supplemental/util/idhash.h"
#include "nng/supplemental/nanolib/log.h"
#ifdef SUPP_PARQUET
#include "nng/supplemental/nanolib/parquet.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stream_node {
	char *name;
	uint8_t id;
	void *(*decode)(void *);
	void *(*encode)(void *);
	void (*encode_stream)(void *, nng_aio *, size_t);
	void *(*cmd_parser)(void *);
} stream_node;

#ifndef SUPP_PARQUET
typedef struct parquet_data parquet_data;
typedef struct parquet_data_ret parquet_data_ret;

typedef struct {
	uint8_t *data;
	uint32_t size;
} parquet_data_packet;


struct parquet_data_ret {
	// Payload_arr should col first.
	uint32_t               col_len;
	uint32_t               row_len;
	uint64_t              *ts;
	char                 **schema;
	parquet_data_packet ***payload_arr;
};

struct parquet_data {
	// Payload_arr should col first.
	// First column of schema should be
	// ts, can not be changed.
	// col_len is payload_arr col_len,
	// schema len = col_len + 1, 1 is ts col.
	uint32_t               col_len;
	uint32_t               row_len;
	uint64_t              *ts;
	char                 **schema;
	parquet_data_packet ***payload_arr;
};

parquet_data *parquet_data_alloc(char **schema,
								 parquet_data_packet ***payload_arr,
								 uint64_t *ts,
								 uint32_t col_len,
								 uint32_t row_len);

void parquet_data_free(parquet_data *data);
#endif

struct stream_data_out {
	uint32_t col_len;
	uint32_t row_len;
	uint64_t *ts;
	char **schema;
	parquet_data_packet ***payload_arr;
};

struct stream_data_in {
	void **datas;
	uint64_t *keys;
	uint32_t *lens;
	uint32_t len;
};

struct stream_decoded_data {
	void *data;
	uint32_t len;
};

struct cmd_data {
	bool is_sync;
	uint64_t start_key;
	uint64_t end_key;
	uint32_t schema_len;
	char **schema;
};

// Unified registration API: supports optional streaming encode callback (may be NULL)
int stream_register(char *name, uint8_t id,
					void *(*decode)(void *),
					void *(*encode)(void *),
					void (*encode_stream)(void *, nng_aio *, size_t),
					void *(*cmd_parser)(void *));
int stream_unregister(uint8_t id);
void *stream_decode(uint8_t id, void *buf);
void *stream_encode(uint8_t id, void *buf);
int stream_encode_stream(uint8_t id, void *buf, nng_aio *aio, size_t chunk_bytes);
void *stream_cmd_parser(uint8_t id, void *buf);
int stream_sys_init(void);
void stream_sys_fini(void);

void stream_decoded_data_free(struct stream_decoded_data *data);
void stream_data_out_free(struct stream_data_out *data);
void stream_data_in_free(struct stream_data_in *sdata);

#ifdef __cplusplus
}
#endif
#endif
