// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT
#ifndef NNG_SUPPLEMENTAL_NANOLIB_PARQUET_DATA_H
#define NNG_SUPPLEMENTAL_NANOLIB_PARQUET_DATA_H

#include "nng/nng.h"

typedef struct parquet_data parquet_data;

typedef struct {
	uint8_t *data;
	uint32_t size;
} parquet_data_packet;

typedef struct parquet_data_ret {
	uint32_t               col_len;
	uint32_t               row_len;
	uint64_t              *ts;
	char                 **schema;
	parquet_data_packet ***payload_arr; // Indexed by column, then row.
} parquet_data_ret;

#ifdef __cplusplus
extern "C" {
#endif

/* Standalone Stream exports these helpers for plugins. */
#ifdef SUPP_PARQUET
#define NNG_PARQUET_DATA_DECL extern
#else
#define NNG_PARQUET_DATA_DECL NNG_DECL
#endif

NNG_PARQUET_DATA_DECL parquet_data *parquet_data_alloc(char **schema,
    parquet_data_packet ***payload_arr, uint64_t *ts, uint32_t col_len,
    uint32_t row_len);
NNG_PARQUET_DATA_DECL void parquet_data_free(parquet_data *data);
#undef NNG_PARQUET_DATA_DECL

#ifdef __cplusplus
}
#endif

#endif
