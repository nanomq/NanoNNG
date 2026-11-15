// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT
#ifndef NNG_SOMEIP_BATCH_H
#define NNG_SOMEIP_BATCH_H

#include "nng/exchange/stream/someip_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dedicated storage for the five SOME/IP columns. No generic schema tree. */
typedef struct someip_batch {
	uint32_t rows;
	uint64_t *ts;
	uint64_t *batch_index;
	uint64_t *frame_index;
	int32_t *session_id;
	uint32_t *offsets; /* rows + 1, contiguous template bytes */
	uint8_t *templates;
	uint32_t template_size;
	bool session_delta;
	uint8_t version; /* 0: unchecked; 2/3: original SHA-256 domains */
	char manifest[84];
	uint64_t start_key; /* read selection; validation always covers all rows */
	uint64_t end_key;
} someip_batch;

someip_batch *someip_batch_alloc(uint32_t rows, uint32_t bytes);
void someip_batch_free(void *);
/* Returns 0 for a structurally valid batch with a matching optional digest. */
int someip_batch_validate(const someip_batch *);
char **someip_batch_schema(const someip_batch *);
parquet_data *parquet_data_alloc_someip(someip_batch *);
/* Takes no ownership. Result text is freed with stream_decoded_data_free(). */
void *someip_batch_decode(const someip_batch *, uint64_t start, uint64_t end);

#ifdef __cplusplus
}
#endif
#endif
