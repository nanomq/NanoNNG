#include <arrow/io/file.h>
#include <arrow/util/key_value_metadata.h>
#include <parquet/column_writer.h>
#include <parquet/file_reader.h>
#include <parquet/file_writer.h>
#include <parquet/schema.h>

#include <cstring>
#include <string>

/* Read the physical column chunks so per-leaf statistics cannot accidentally
 * disable timestamp/index statistics or override a global opt-out. */
extern "C" int
pq_test_column_statistics(const char *path, const char *column, int expected)
{
	try {
		auto reader = parquet::ParquetFileReader::OpenFile(path, false);
		auto metadata = reader->metadata();
		int found = 0;
		for (int row = 0; row < metadata->num_row_groups(); row++) {
			auto group = metadata->RowGroup(row);
			for (int col = 0; col < group->num_columns(); col++) {
				auto chunk = group->ColumnChunk(col);
				if (chunk->path_in_schema()->ToDotString() == column) {
					found++;
					if (chunk->is_stats_set() != (expected != 0)) {
						return -1;
					}
				}
			}
		}
		return found == metadata->num_row_groups() && found > 0 ? 0 : -1;
	} catch (const std::exception &) {
		return -1;
	}
}

/* Inspect the physical footer, independently of the batch reader. */
extern "C" int
pq_test_metadata_footer(const char *path, const char *metadata,
    const char *topic, const char *cipher)
{
	try {
		auto reader = parquet::ParquetFileReader::OpenFile(path, false);
		auto kv = reader->metadata()->key_value_metadata();
		if (kv == nullptr || kv->FindKey("ARROW:schema") >= 0) {
			return -1;
		}
		int codec_count = 0;
		for (int64_t i = 0; i < kv->size(); i++) {
			if (kv->key(i) == "nmq.codec.metadata") {
				codec_count++;
				if (metadata == nullptr || kv->value(i) != metadata) {
					return -1;
				}
			}
		}
		if (codec_count != (metadata != nullptr ? 1 : 0)) {
			return -1;
		}
		int topic_idx = kv->FindKey("nmq.topic");
		int cipher_idx = kv->FindKey("nmq.enc.cipher");
		return topic_idx >= 0 && kv->value(topic_idx) == topic &&
		        cipher_idx >= 0 && kv->value(cipher_idx) == cipher
		    ? 0 : -1;
	} catch (const std::exception &) {
		return -1;
	}
}

/* Bypass the NanoMQ writer to create legacy and malformed footer fixtures. */
extern "C" int
pq_test_write_metadata_fixture(const char *path, const char *value,
    size_t value_len, int duplicate)
{
	try {
		auto maybe_out = arrow::io::FileOutputStream::Open(path);
		if (!maybe_out.ok()) {
			return -1;
		}
		auto out = *maybe_out;
		auto schema = std::static_pointer_cast<parquet::schema::GroupNode>(
		    parquet::schema::GroupNode::Make("schema",
		        parquet::Repetition::REQUIRED,
		        { parquet::schema::PrimitiveNode::Make("ts",
		            parquet::Repetition::REQUIRED, parquet::Type::INT64) }));
		std::shared_ptr<arrow::KeyValueMetadata> kv;
		if (value != nullptr) {
			kv = std::make_shared<arrow::KeyValueMetadata>();
			kv->Append("nmq.codec.metadata", std::string(value, value_len));
			if (duplicate) {
				kv->Append("nmq.codec.metadata", std::string(value, value_len));
			}
		}
		auto writer = parquet::ParquetFileWriter::Open(
		    out, schema, parquet::default_writer_properties(), kv);
		auto group = writer->AppendRowGroup();
		auto col = static_cast<parquet::Int64Writer *>(group->NextColumn());
		int64_t ts = 42;
		col->WriteBatch(1, nullptr, nullptr, &ts);
		group->Close();
		writer->Close();
		return out->Close().ok() ? 0 : -1;
	} catch (const std::exception &) {
		return -1;
	}
}
