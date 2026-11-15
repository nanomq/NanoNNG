// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT
#ifndef NNG_SOMEIP_PARQUET_H
#define NNG_SOMEIP_PARQUET_H

#include "mqtt/protocol/exchange/stream/someip_batch.h"
#include <parquet/file_reader.h>
#include <parquet/file_writer.h>
#include <parquet/schema.h>

std::shared_ptr<parquet::schema::GroupNode> someip_parquet_schema(
    const someip_batch *batch);
void someip_parquet_encoding(parquet::WriterProperties::Builder &builder,
    const someip_batch *batch);
void someip_parquet_write_columns(parquet::RowGroupWriter *group,
    const someip_batch *batch);
bool someip_parquet_is_file(const parquet::FileMetaData &metadata);
/* Throws on invalid/malformed data; the caller owns the returned batch. */
someip_batch *someip_parquet_read_batch(parquet::ParquetFileReader &reader,
    uint64_t start, uint64_t end, const char **projection, uint16_t count);

#endif
