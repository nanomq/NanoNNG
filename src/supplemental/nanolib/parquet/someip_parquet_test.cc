// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT
#include "someip_parquet.h"
#include "nng/exchange/stream/someip_stream.h"
#include <arrow/util/key_value_metadata.h>
#include <arrow/io/memory.h>
#include <parquet/column_page.h>
#include <parquet/column_reader.h>
#include <nuts.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

static bool
recognized(const parquet::schema::NodeVector &fields, bool with_manifest)
{
	parquet::SchemaDescriptor schema;
	schema.Init(parquet::schema::GroupNode::Make(
	    "schema", parquet::Repetition::REQUIRED, fields));
	auto metadata = parquet::FileMetaDataBuilder::Make(&schema,
	    parquet::WriterProperties::Builder().build())->Finish(
	    with_manifest ? arrow::key_value_metadata(
	        { "nmq.codec.metadata" }, { "another-codec" }) : nullptr);
	return someip_parquet_is_file(*metadata);
}

static void
test_schema_recognition(void)
{
	using parquet::schema::PrimitiveNode;
	using parquet::Repetition;
	using parquet::Type;
	for (uint8_t version : { 0, 2, 3 }) {
		someip_batch batch = {};
		batch.version = version;
		auto schema = someip_parquet_schema(&batch);
		parquet::schema::NodeVector fields;
		for (int c = 0; c < 5; ++c) fields.push_back(schema->field(c));
		NUTS_TRUE(recognized(fields, false));
		NUTS_TRUE(recognized(fields, true));
		for (int c = 0; c < 5; ++c) {
			auto saved = fields[c];
			fields[c] = PrimitiveNode::Make("other", Repetition::REQUIRED,
			    c < 3 ? Type::INT64 : c == 3 ? Type::INT32 : Type::BYTE_ARRAY);
			NUTS_TRUE(!recognized(fields, true));
			fields[c] = PrimitiveNode::Make(saved->name(), Repetition::REQUIRED,
			    c < 4 ? Type::BYTE_ARRAY : Type::INT32);
			NUTS_TRUE(!recognized(fields, true));
			fields[c] = saved;
		}
		fields[4] = PrimitiveNode::Make("message_template_future",
		    Repetition::REQUIRED, Type::BYTE_ARRAY);
		NUTS_TRUE(!recognized(fields, true));
		fields.pop_back();
		NUTS_TRUE(!recognized(fields, true));
	}
	parquet::schema::NodeVector raw = {
		PrimitiveNode::Make("ts", Repetition::REQUIRED, Type::INT64),
		PrimitiveNode::Make("data", Repetition::REQUIRED, Type::BYTE_ARRAY),
	};
	NUTS_TRUE(!recognized(raw, false));
	NUTS_TRUE(!recognized(raw, true));
	raw[1] = PrimitiveNode::Make("value", Repetition::REQUIRED, Type::INT32);
	NUTS_TRUE(!recognized(raw, false));
	NUTS_TRUE(!recognized(raw, true));
}

static void
test_session_wrap_encoding(void)
{
	uint8_t frames[4][16] = {};
	someip_record records[4] = {};
	for (unsigned r = 0; r < 4; ++r) {
		uint16_t session = static_cast<uint16_t>(65534 + r);
		frames[r][7] = 8; // SOME/IP length excludes the first eight bytes.
		frames[r][10] = static_cast<uint8_t>(session >> 8);
		frames[r][11] = static_cast<uint8_t>(session);
		records[r] = { r, 10 + r / 2, r % 2, frames[r], 16 };
	}
	parquet_data *encoded = someip_encode_records(records, 4, true);
	NUTS_ASSERT(encoded != nullptr);
	conf_parquet conf = {};
	conf_parquet_init(&conf);
	conf.encryption.enable = false;
	char path[256];
	const char *tmp = std::getenv("TMPDIR");
	if (tmp == nullptr) tmp = "/tmp";
	NUTS_TRUE(std::snprintf(path, sizeof(path), "%s/nng-someip-delta-%08x.parquet",
	    tmp, nng_random()) < static_cast<int>(sizeof(path)));
	NUTS_PASS(someip_parquet_write_file(&conf, path, encoded, "someip/delta"));
	auto reader = parquet::ParquetFileReader::OpenFile(path);
	auto column = reader->metadata()->RowGroup(0)->ColumnChunk(3);
	const auto &encodings = column->encodings();
	NUTS_TRUE(std::find(encodings.begin(), encodings.end(),
	    parquet::Encoding::DELTA_BINARY_PACKED) != encodings.end());
	reader->Close();
	parquet_data_ret *read = someip_parquet_read_file(&conf, path, 0, INT64_MAX);
	NUTS_ASSERT(read != nullptr && read->someip_data != nullptr);
	auto batch = static_cast<someip_batch *>(read->someip_data);
	NUTS_TRUE(batch->session_delta && batch->rows == 4);
	for (unsigned r = 0; r < 4; ++r) {
		NUTS_TRUE(batch->session_id[r] == static_cast<uint16_t>(65534 + r));
	}
	NUTS_PASS(someip_batch_validate(batch));
	someip_parquet_ret_free(read);
	parquet_data_free(encoded);
	NUTS_TRUE(std::remove(path) == 0);
}

/* Point one first-group column at a shorter/longer column in another group.
 * Keep the declared group/file row counts intact, so the reader must detect
 * the mismatch in the column rather than trusting only footer counts. */
static std::shared_ptr<parquet::FileMetaData>
replace_column(const parquet::FileMetaData &source,
    std::shared_ptr<parquet::WriterProperties> props, int column, int from_group)
{
	auto builder = parquet::FileMetaDataBuilder::Make(source.schema(), props);
	for (int g = 0; g < source.num_row_groups(); ++g) {
		auto group = builder->AppendRowGroup();
		group->set_num_rows(source.RowGroup(g)->num_rows());
		int64_t size = 0;
		for (int c = 0; c < 5; ++c) {
			auto original = source.RowGroup(g == 0 && c == column ? from_group : g)->ColumnChunk(c);
			auto out = group->NextColumnChunk();
			std::map<parquet::Encoding::type, int32_t> dict, data;
			for (const auto &stat : original->encoding_stats()) {
				(stat.page_type == parquet::PageType::DICTIONARY_PAGE ? dict : data)
				    [stat.encoding] += stat.count;
			}
			out->Finish(original->num_values(), original->dictionary_page_offset(),
			    original->index_page_offset(), original->data_page_offset(),
			    original->total_compressed_size(), original->total_uncompressed_size(),
			    original->has_dictionary_page(), false, dict, data);
			size += original->total_compressed_size();
		}
		group->Finish(size, g);
	}
	return builder->Finish(source.key_value_metadata());
}

static void
test_multipage_columns(void)
{
	const uint32_t groups[] = { 2051, 2050, 2052 };
	const uint32_t rows = groups[0] + groups[1] + groups[2];
	std::vector<uint8_t> frames(rows * 32);
	std::vector<someip_record> records(rows);
	for (uint32_t r = 0; r < rows; ++r) {
		auto frame = frames.data() + r * 32;
		frame[7] = 24;
		uint16_t session = static_cast<uint16_t>(65534 + r);
		frame[10] = static_cast<uint8_t>(session >> 8);
		frame[11] = static_cast<uint8_t>(session);
		for (unsigned i = 16; i < 32; ++i) {
			frame[i] = static_cast<uint8_t>((r >> ((i % 4) * 8)) + i);
		}
		records[r] = { r * UINT64_C(104729), r / 2, r % 2, frame, 32 };
	}
	std::unique_ptr<parquet_data, decltype(&parquet_data_free)> encoded(
	    someip_encode_records(records.data(), rows, true), parquet_data_free);
	NUTS_ASSERT(encoded != nullptr);
	auto original = static_cast<someip_batch *>(
	    reinterpret_cast<stream_data_out *>(encoded.get())->someip_data);
	parquet::WriterProperties::Builder properties;
	someip_parquet_encoding(properties, original);
	properties.data_pagesize(64)->write_batch_size(16);
	auto props = properties.build();
	auto sink = arrow::io::BufferOutputStream::Create().ValueOrDie();
	auto writer = parquet::ParquetFileWriter::Open(sink,
	    someip_parquet_schema(original), props,
	    arrow::key_value_metadata({ "nmq.codec.metadata" }, { original->manifest }));
	uint32_t first = 0;
	for (auto count : groups) {
		someip_batch part = *original;
		part.rows = count;
		part.ts += first;
		part.batch_index += first;
		part.frame_index += first;
		part.session_id += first;
		part.offsets += first;
		auto group = writer->AppendRowGroup();
		someip_parquet_write_columns(group, &part);
		group->Close();
		first += count;
	}
	writer->Close();
	auto buffer = sink->Finish().ValueOrDie();
	auto source = std::make_shared<arrow::io::BufferReader>(buffer);
	auto reader = parquet::ParquetFileReader::Open(source);
	/* Prove this fixture spans pages in every physical column. */
	for (int c = 0; c < 5; ++c) {
		auto pages = reader->RowGroup(0)->GetColumnPageReader(c);
		unsigned count = 0;
		while (auto page = pages->NextPage()) {
			if (page->type() != parquet::PageType::DICTIONARY_PAGE) ++count;
		}
		NUTS_TRUE(count > 1);
	}
	std::unique_ptr<someip_batch, decltype(&someip_batch_free)> decoded(
	    someip_parquet_read_batch(*reader, 0, INT64_MAX, nullptr, 0), someip_batch_free);
	NUTS_TRUE(decoded->rows == rows && decoded->session_delta);
	NUTS_TRUE(decoded->template_size == original->template_size);
	NUTS_TRUE(std::memcmp(decoded->templates, original->templates, original->template_size) == 0);
	NUTS_PASS(someip_batch_validate(decoded.get())); // SHA covers all columns.
	for (int column : { 0, 1, 2, 3, 4 }) {
		for (int from_group : { 1, 2 }) {
			auto bad = parquet::ParquetFileReader::Open(source,
			    parquet::default_reader_properties(),
			    replace_column(*reader->metadata(), props, column, from_group));
			bool rejected = false;
			try {
				std::unique_ptr<someip_batch, decltype(&someip_batch_free)> unexpected(
				    someip_parquet_read_batch(*bad, 0, INT64_MAX, nullptr, 0), someip_batch_free);
			} catch (const std::exception &error) {
				const char *expected = from_group == 1 ? "truncated/null" : "extra";
				rejected = std::strstr(error.what(), expected) != nullptr;
			}
			NUTS_TRUE(rejected);
		}
	}
}

NUTS_TESTS = {
	{ "SOME/IP exact schema recognition", test_schema_recognition },
	{ "SOME/IP session wrap delta encoding", test_session_wrap_encoding },
	{ "SOME/IP multi-page columns and row count validation", test_multipage_columns },
	{ nullptr, nullptr },
};
