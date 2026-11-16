// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT
#include "someip_parquet.h"

#include <parquet/column_reader.h>
#include <parquet/column_writer.h>
#include <arrow/util/key_value_metadata.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char *message)
{
	if (!value) throw std::runtime_error(message);
}

uint8_t version_for_name(const std::string &name)
{
	if (name == "message_template") return 0;
	if (name == "message_template_v2") return 2;
	if (name == "message_template_v3") return 3;
	throw std::runtime_error("unsupported SOME/IP template column");
}

template <typename Reader, typename Value>
void read_required_values(const std::shared_ptr<Reader> &column, Value *values,
    uint32_t expected)
{
	uint32_t used = 0;
	while (used < expected) {
		int64_t read = 0;
		int64_t actual = column->ReadBatch(expected - used, nullptr, nullptr,
		    values + used, &read);
		/* ReadBatch may stop at a data page boundary. A short positive read
		 * is progress; zero, nulls or an overrun are malformed input. */
		require(actual > 0 && actual <= expected - used && read == actual,
		    "truncated/null SOME/IP integer column");
		used += static_cast<uint32_t>(actual);
	}
	require(!column->HasNext(), "extra SOME/IP integer rows");
}
}

std::shared_ptr<parquet::schema::GroupNode>
someip_parquet_schema(const someip_batch *batch)
{
	using parquet::schema::PrimitiveNode;
	using parquet::Repetition;
	using parquet::Type;
	using parquet::ConvertedType;
	auto names = someip_batch_schema(batch);
	parquet::schema::NodeVector fields;
	/* The source codec's PQ_INT64 writer maps to Arrow uint64. Preserve
	 * that logical type as well as the INT64 physical representation. */
	for (unsigned c = 0; c < 3; ++c) {
		fields.push_back(PrimitiveNode::Make(names[c], Repetition::REQUIRED,
		    Type::INT64, ConvertedType::UINT_64));
	}
	fields.push_back(PrimitiveNode::Make(names[3], Repetition::REQUIRED,
	    Type::INT32));
	fields.push_back(PrimitiveNode::Make(names[4], Repetition::REQUIRED,
	    Type::BYTE_ARRAY));
	return std::static_pointer_cast<parquet::schema::GroupNode>(
	    parquet::schema::GroupNode::Make("schema", Repetition::REQUIRED, fields));
}

void
someip_parquet_encoding(parquet::WriterProperties::Builder &builder,
    const someip_batch *batch)
{
	auto names = someip_batch_schema(batch);
	for (unsigned c = 0; c < 4; ++c) {
		builder.disable_dictionary(names[c]);
		builder.encoding(names[c], c == 0 || (c == 3 && !batch->session_delta) ?
		    parquet::Encoding::PLAIN : parquet::Encoding::DELTA_BINARY_PACKED);
	}
	builder.enable_dictionary(names[4]);
	builder.disable_statistics(names[4]);
}

void
someip_parquet_write_columns(parquet::RowGroupWriter *group,
    const someip_batch *batch)
{
	const uint64_t *integers[] = { batch->ts, batch->batch_index, batch->frame_index };
	for (unsigned c = 0; c < 3; ++c) {
		auto column = static_cast<parquet::Int64Writer *>(group->NextColumn());
		column->WriteBatch(batch->rows, nullptr, nullptr,
		    reinterpret_cast<const int64_t *>(integers[c]));
		column->Close();
	}
	auto sessions = static_cast<parquet::Int32Writer *>(group->NextColumn());
	sessions->WriteBatch(batch->rows, nullptr, nullptr, batch->session_id);
	sessions->Close();
	auto templates = static_cast<parquet::ByteArrayWriter *>(group->NextColumn());
	/* Small descriptor batches borrow the owned contiguous byte slab; no
	 * per-frame allocation or Arrow BinaryBuilder payload copy is needed. */
	std::vector<parquet::ByteArray> values(std::min(batch->rows, UINT32_C(1024)));
	for (uint32_t first = 0; first < batch->rows;) {
		uint32_t n = std::min(batch->rows - first, (uint32_t) values.size());
		for (uint32_t i = 0; i < n; ++i) {
			uint32_t row = first + i;
			values[i].ptr = batch->templates + batch->offsets[row];
			values[i].len = batch->offsets[row + 1] - batch->offsets[row];
		}
		templates->WriteBatch(n, nullptr, nullptr, values.data());
		first += n;
	}
	templates->Close();
}

bool
someip_parquet_is_file(const parquet::FileMetaData &metadata)
{
	auto schema = metadata.schema();
	if (metadata.num_columns() != 5 || schema->group_node()->field_count() != 5) {
		return false;
	}
	const char *numeric[] = { "ts", "batch_index", "frame_index", "session_id" };
	for (int c = 0; c < 5; ++c) {
		auto column = schema->Column(c);
		auto expected = c < 3 ? parquet::Type::INT64 :
		    c == 3 ? parquet::Type::INT32 : parquet::Type::BYTE_ARRAY;
		if (column->physical_type() != expected ||
		    column->path()->ToDotString() != column->name() ||
		    (c < 4 && column->name() != numeric[c])) return false;
	}
	const auto &name = schema->Column(4)->name();
	return name == "message_template" || name == "message_template_v2" ||
	    name == "message_template_v3";
}

someip_batch *
someip_parquet_read_batch(parquet::ParquetFileReader &reader,
    uint64_t start, uint64_t end, const char **projection, uint16_t count)
{
	auto metadata = reader.metadata();
	require(start <= end && metadata->num_columns() == 5 &&
	    metadata->schema()->group_node()->field_count() == 5,
	    "invalid SOME/IP schema or range");
	const char *numeric[] = { "ts", "batch_index", "frame_index", "session_id" };
	for (int c = 0; c < 5; ++c) {
		auto column = metadata->schema()->Column(c);
		require(column->max_definition_level() == 0 && column->max_repetition_level() == 0 &&
		    column->path()->ToDotString() == column->name(),
		    "nullable/nested SOME/IP column");
		auto expected = c < 3 ? parquet::Type::INT64 :
		    c == 3 ? parquet::Type::INT32 : parquet::Type::BYTE_ARRAY;
		require(column->physical_type() == expected, "wrong SOME/IP column type");
		if (c < 4) require(column->name() == numeric[c], "wrong SOME/IP column name");
	}
	uint8_t version = version_for_name(metadata->schema()->Column(4)->name());
	if (count != 0) {
		require(projection != nullptr && count == 5, "incomplete SOME/IP projection");
		for (int c = 0; c < 5; ++c) {
			unsigned matches = 0;
			for (uint16_t j = 0; j < count; ++j) {
				if (projection[j] != nullptr &&
				    metadata->schema()->Column(c)->name() == projection[j]) matches++;
			}
			require(matches == 1, "incomplete/duplicate SOME/IP projection");
		}
	}
	std::string manifest;
	unsigned manifests = 0;
	auto kv = metadata->key_value_metadata();
	if (kv != nullptr) {
		for (int64_t i = 0; i < kv->size(); ++i) {
			if (kv->key(i) == "nmq.codec.metadata") {
				manifests++;
				manifest = kv->value(i);
			}
		}
	}
	require(manifests == (version == 0 ? 0u : 1u), "missing/duplicate SOME/IP manifest");
	if (version != 0) {
		require(manifest.size() == 83 && manifest.find('\0') == std::string::npos,
		    "invalid SOME/IP manifest");
	}
	auto nrows = metadata->num_rows();
	require(nrows > 0 && (uint64_t) nrows <= (uint64_t) INT32_MAX / 16,
	    "invalid SOME/IP row count");
	uint32_t rows = (uint32_t) nrows;
	require((uint64_t) rows + 1 <= SIZE_MAX / sizeof(uint32_t),
	    "SOME/IP offsets overflow");
	std::vector<uint32_t> offsets((size_t) rows + 1);
	std::vector<uint8_t> bytes;
	uint32_t row = 0;
	std::vector<parquet::ByteArray> values(1024);
	/* Read all templates before allocating the final contiguous batch, so
	 * malformed counts and offsets cannot escape the owning C structure. */
	for (int g = 0; g < metadata->num_row_groups(); ++g) {
		auto group = reader.RowGroup(g);
		auto expected = metadata->RowGroup(g)->num_rows();
		require(expected >= 0 && (uint64_t) expected <= rows - row,
		    "invalid SOME/IP row group size");
		auto column = std::static_pointer_cast<parquet::ByteArrayReader>(group->Column(4));
		for (int64_t used = 0; used < expected;) {
			int64_t n = std::min<int64_t>(1024, expected - used), read = 0;
			int64_t actual = column->ReadBatch(n, nullptr, nullptr, values.data(), &read);
			require(actual > 0 && actual <= n && read == actual,
			    "truncated/null SOME/IP template column");
			/* ByteArray values borrow the current page's decoder storage.
			 * Copy this read before advancing to another page. */
			for (int64_t i = 0; i < actual; ++i) {
				const auto &value = values[i];
				require(value.ptr != nullptr && value.len >= 16 &&
				    value.len <= INT32_MAX - bytes.size(), "invalid SOME/IP template size");
				offsets[row++] = (uint32_t) bytes.size();
				bytes.insert(bytes.end(), value.ptr, value.ptr + value.len);
			}
			used += actual;
		}
		require(!column->HasNext(), "extra SOME/IP template rows");
	}
	require(row == rows, "inconsistent SOME/IP row groups");
	offsets[rows] = (uint32_t) bytes.size();
	std::unique_ptr<someip_batch, decltype(&someip_batch_free)> batch(
	    someip_batch_alloc(rows, (uint32_t) bytes.size()), someip_batch_free);
	require(batch != nullptr, "SOME/IP allocation failed");
	std::memcpy(batch->offsets, offsets.data(), offsets.size() * sizeof(uint32_t));
	std::memcpy(batch->templates, bytes.data(), bytes.size());
	batch->version = version;
	batch->start_key = start;
	batch->end_key = end;
	if (version != 0) std::memcpy(batch->manifest, manifest.c_str(), manifest.size() + 1);
	row = 0;
	for (int g = 0; g < metadata->num_row_groups(); ++g) {
		auto group = reader.RowGroup(g);
		uint32_t n = (uint32_t) metadata->RowGroup(g)->num_rows();
		uint64_t *integers[] = { batch->ts, batch->batch_index, batch->frame_index };
		for (int c = 0; c < 4; ++c) {
			if (c < 3) {
				auto column = std::static_pointer_cast<parquet::Int64Reader>(group->Column(c));
				read_required_values(column,
				    reinterpret_cast<int64_t *>(integers[c] + row), n);
			} else {
				auto column = std::static_pointer_cast<parquet::Int32Reader>(group->Column(c));
				read_required_values(column, batch->session_id + row, n);
			}
		}
		row += n;
	}
	batch->session_delta = true;
	uint16_t previous = 0, step = 0;
	for (uint32_t r = 0; r < rows; ++r) {
		uint16_t session = (uint16_t) batch->session_id[r];
		uint16_t delta = (uint16_t) (session - previous);
		if (r == 1) step = delta;
		else if (r > 1 && delta != step) batch->session_delta = false;
		previous = session;
	}
	require(someip_batch_validate(batch.get()) == 0, "invalid SOME/IP batch or digest");
	return batch.release();
}
