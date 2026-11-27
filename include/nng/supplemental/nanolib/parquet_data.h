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
	/* Dedicated SOME/IP columns; NULL for the existing raw/SPI/CANP layout. */
	void                  *someip_data;
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
/* Payload bytes remain owned by the caller's live messages. */
NNG_PARQUET_DATA_DECL void parquet_data_payloads_borrowed(parquet_data *data);
/* The timestamp array remains owned by the caller. */
NNG_PARQUET_DATA_DECL void parquet_data_ts_borrowed(parquet_data *data);
/* Both the schema array and its strings remain owned by the caller. */
NNG_PARQUET_DATA_DECL void parquet_data_schema_borrowed(parquet_data *data);
/* Packet structs are one contiguous allocation per column. */
NNG_PARQUET_DATA_DECL void parquet_data_packets_contiguous(parquet_data *data);
#undef NNG_PARQUET_DATA_DECL

#ifdef __cplusplus
}
#endif

#endif
