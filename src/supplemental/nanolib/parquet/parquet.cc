#include <arrow/api.h>
#include <arrow/array.h>
#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_nested.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/io/file.h>
#include <arrow/status.h>
#include <arrow/util/key_value_metadata.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>

// Select Arrow encryption key API.
// Default 14: std::string keys + FileDecryptionProperties::DeepClone().
// SecureString APIs landed in Arrow 22.0.0 (GH-31603 / #46017); set
// NNG_ARROW_VERSION_MAJOR>=22 (CMake -DNNG_ARROW_VERSION_MAJOR=22).
#ifndef NNG_ARROW_VERSION_MAJOR
#define NNG_ARROW_VERSION_MAJOR 14
#endif
#if NNG_ARROW_VERSION_MAJOR >= 22
#include <arrow/util/secure_string.h>
#endif

#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/md5.h"
#include "nng/supplemental/nanolib/parquet.h"
#include "nng/supplemental/nanolib/queue.h"
#include "parquet_file_manager.h"
#include <assert.h>
#include <atomic>
#include <dirent.h>
#include <fstream>
#include <inttypes.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <errno.h>
#include <sys/stat.h>
#include <thread>
#include <ctime>
#include <vector>
#include <dirent.h>
#include <regex.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

using namespace std;
using parquet::ConvertedType;
using parquet::Encoding;
using parquet::Repetition;
using parquet::Type;
using parquet::schema::GroupNode;
using parquet::schema::PrimitiveNode;
#define PARQUET_END 1024

struct SchemaColumn {
	char                             *name;
	shared_ptr<parquet::ColumnReader> reader;
};

#define DO_IT_IF_NOT_NULL(func, arg1, arg2) \
	if (arg1) {                         \
		func(arg1, arg2);           \
	}

#define FREE_IF_NOT_NULL(free, size) DO_IT_IF_NOT_NULL(nng_free, free, size)

#define _Atomic(X) std::atomic<X>
atomic_bool is_available = false;
#define WAIT_FOR_AVAILABLE    \
	while (!is_available) \
		nng_msleep(10);

#define UINT64_MAX_DIGITS 20

#if NNG_ARROW_VERSION_MAJOR >= 22
static arrow::util::SecureString
parquet_make_secure_string(const char *key)
{
	std::string key_str(key);
	return arrow::util::SecureString(std::move(key_str));
}

class UniformKeyRetriever : public parquet::DecryptionKeyRetriever {
	arrow::util::SecureString key_;

public:
	explicit UniformKeyRetriever(const char *key)
	    : key_(parquet_make_secure_string(key))
	{
	}
	arrow::util::SecureString
	GetKey(const std::string &) override
	{
		return key_;
	}
};
#else
class UniformKeyRetriever : public parquet::DecryptionKeyRetriever {
	std::string key_;

public:
	explicit UniformKeyRetriever(const std::string &key) : key_(key) {}
	std::string
	GetKey(const std::string &) override
	{
		return key_;
	}
};
#endif

parquet_file_manager file_manager;
CircularQueue        parquet_queue;
pthread_mutex_t      parquet_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t       parquet_queue_not_empty = PTHREAD_COND_INITIALIZER;

static bool parquet_resolve_and_set_decryption_properties(
	parquet::ReaderProperties &reader_properties, conf_parquet *conf,
	const char *filename);
static int parquet_check_is_compat_and_decrypt(
	char *filename, bool &is_compat_mode, bool &is_encrypted);

static char *
get_file_name(conf_parquet
 *conf, uint64_t key_start, uint64_t key_end)
{
	char *file_name = NULL;
	char *dir       = conf->dir;
	char *prefix    = conf->file_name_prefix;

	size_t prefix_len = prefix != NULL ? strlen(prefix) : 0;

	file_name = (char *) malloc(prefix_len + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}


	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".parquet", dir,
	    prefix, key_start, key_end);
	return file_name;
}

string
gen_random(const int len)
{
	static const char alphanum[] = "0123456789"
	                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                               "abcdefghijklmnopqrstuvwxyz";
	string       tmp_s;
	tmp_s.reserve(len);

	for (int i = 0; i < len; ++i) {
		tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
	}
	return tmp_s;
}

static bool
parquet_ensure_dir(const char *dir)
{
	struct stat st;
	if (dir == NULL || dir[0] == '\0') {
		return false;
	}
	if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
		return true;
	}
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
		log_error("Failed to create directory %s, errno: %d", dir,
		    errno);
		return false;
	}
	if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
		return true;
	}
	log_error("Directory %s is not available after create", dir);
	return false;
}

static char *
get_random_file_name(conf_parquet *conf, char *prefix, uint64_t key_start,
    uint64_t key_end)
{
	char       *file_name = NULL;
	const char *dir       = "/tmp";

	if (conf != NULL && conf->tmp_dir != NULL && conf->tmp_dir[0] != '\0') {
		dir = conf->tmp_dir;
	}
	if (!parquet_ensure_dir(dir)) {
		log_error("Abort temp parquet filename: cannot use dir %s",
		    dir);
		return NULL;
	}

	file_name = (char *) malloc(strlen(prefix) + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}

	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".parquet", dir,
	    prefix, key_start, key_end);
	return file_name;
}

static shared_ptr<GroupNode>
setup_schema(char **schema, uint32_t schema_col)
{
	parquet::schema::NodeVector fields;
	if (NULL == schema[0]) {
		log_error("Schema value is NULL!");
		return NULL;
	}
	// Set ts column
	fields.push_back(
	    PrimitiveNode::Make(schema[0], parquet::Repetition::REQUIRED,
	        parquet::Type::INT64, parquet::ConvertedType::UINT_64));

	// Set data column(like canid+busid or raw data...)
	for (uint32_t i = 1; i < schema_col; i++) {
		if (NULL == schema[i]) {
			log_error("Schema value is NULL!");
			return NULL;
		}
		fields.push_back(
		    PrimitiveNode::Make(schema[i], Repetition::OPTIONAL,
		        Type::BYTE_ARRAY, ConvertedType::NONE));
	}

	return static_pointer_cast<GroupNode>(
	    GroupNode::Make("schema", Repetition::REQUIRED, fields));
}

parquet_file_range *
parquet_file_range_alloc(uint32_t start_idx, uint32_t end_idx, char *filename)
{
	parquet_file_range *range = new parquet_file_range;
	range->start_idx          = start_idx;
	range->end_idx            = end_idx;
	range->filename           = nng_strdup(filename);
	return range;
}

void
parquet_file_range_free(parquet_file_range *range)
{
	if (range) {
		FREE_IF_NOT_NULL(range->filename, strlen(range->filename));
		delete range;
	}
}

parquet_data *
parquet_data_alloc(char **schema, parquet_data_packet ***payload_arr,
    uint64_t *ts, uint32_t col_len, uint32_t row_len)
{
	if (payload_arr == NULL || schema == NULL || col_len == 0 ||
	    row_len == 0) {
		log_error("payload || schema should not be NULL, col || row "
		          "len should't == 0");
	}
	parquet_data *data = new parquet_data;
	if (data == NULL) {
		return NULL; // Memory allocation failed
	}
	data->ts          = ts;
	data->col_len     = col_len + 1;
	data->row_len     = row_len;
	data->schema      = schema;
	data->payload_arr = payload_arr;
	data->schema_tree = NULL;
	data->root        = NULL;
	if (pq_batch_attach_flat(data) != 0) {
		log_warn("pq_batch_attach_flat failed; using legacy flat buffers");
		data->schema_tree = NULL;
		data->root        = NULL;
	}
	return data;
}

void
parquet_data_free(parquet_data *data)
{
	if (data == NULL) {
		return;
	}
	if (data->root != NULL) {
		pq_array_free(data->root);
		data->root = NULL;
		if (data->payload_arr == NULL) {
			data->ts = NULL;
		}
	}
	if (data->schema_tree != NULL) {
		pq_type_free(data->schema_tree);
		data->schema_tree = NULL;
	}
	if (data->payload_arr != NULL && data->schema != NULL &&
	    data->col_len > 0) {
		for (uint32_t c = 0; c < data->col_len - 1; c++) {
			FREE_IF_NOT_NULL(
			    data->schema[c], strlen(data->schema[c]));
			for (uint32_t r = 0; r < data->row_len; r++) {
				parquet_data_packet *payload =
				    data->payload_arr[c][r];
				if (payload && payload->data &&
				    payload->size > 0) {
					nng_free(payload->data, payload->size);
				}
				FREE_IF_NOT_NULL(payload, sizeof(*payload));
			}
			FREE_IF_NOT_NULL(data->payload_arr[c], data->row_len);
		}

		FREE_IF_NOT_NULL(data->schema[data->col_len - 1],
		    strlen(data->schema[data->col_len - 1]));
		FREE_IF_NOT_NULL(data->schema, data->col_len);
		FREE_IF_NOT_NULL(data->ts, data->row_len);
		FREE_IF_NOT_NULL(data->payload_arr, data->col_len);
	}
	delete data;
}

parquet_object *
parquet_object_alloc(parquet_data *data, parquet_type type, nng_aio *aio,
    void *aio_arg, char *topic)
{
	parquet_object *elem = new parquet_object;
	elem->data           = data;
	elem->type           = type;
	elem->aio            = aio;
	elem->aio_arg        = aio_arg;
	elem->topic          = topic;
	elem->ranges        = new parquet_file_ranges;
	elem->ranges->range = NULL;
	elem->ranges->start = 0;
	elem->ranges->size  = 0;
	return elem;
}

void
parquet_object_free(parquet_object *elem)
{
	if (elem) {
		if (elem->data) {
			parquet_data_free(elem->data);
		}
		nng_aio_set_prov_data(elem->aio, elem->aio_arg);
		nng_aio_set_output(elem->aio, 1, elem->ranges);
		log_debug("finish write aio");
		DO_IT_IF_NOT_NULL(nng_aio_finish_sync, elem->aio, 0);

		for (int i = 0; i < elem->ranges->size; i++) {
			parquet_file_range_free(elem->ranges->range[i]);
		}
		free(elem->ranges->range);
		delete elem->ranges;
		delete elem;
	}
}

int
parquet_write_batch_async(parquet_object *elem)
{
	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	log_debug("WAIT_FOR_AVAILABLE");
	WAIT_FOR_AVAILABLE
	log_debug("WAIT_FOR parquet_queue_mutex");
	pthread_mutex_lock(&parquet_queue_mutex);
	if (IS_EMPTY(parquet_queue)) {
		pthread_cond_broadcast(&parquet_queue_not_empty);
		log_debug("broadcast signal!");
	}
	ENQUEUE(parquet_queue, elem);
	log_debug("enqueue element.");

	pthread_mutex_unlock(&parquet_queue_mutex);

	return 0;
}

int
parquet_write_batch_tmp_async(parquet_object *elem)
{
	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	elem->type = WRITE_TEMP_RAW;
	log_debug("WAIT_FOR_AVAILABLE");
	WAIT_FOR_AVAILABLE
	log_debug("WAIT_FOR parquet_queue_mutex");
	pthread_mutex_lock(&parquet_queue_mutex);
	if (IS_EMPTY(parquet_queue)) {
		pthread_cond_broadcast(&parquet_queue_not_empty);
	}
	ENQUEUE(parquet_queue, elem);
	log_debug("enqueue element.");

	pthread_mutex_unlock(&parquet_queue_mutex);

	return 0;
}

shared_ptr<parquet::FileEncryptionProperties>
parquet_set_encryption(char **schema_arr, uint32_t schema_len, conf_parquet *conf)
{
	shared_ptr<parquet::FileEncryptionProperties> encryption_configurations;

	// Encrypt all columns with a same key. left footer plain (uniform encryption)
	std::map<std::string, std::shared_ptr<parquet::ColumnEncryptionProperties>>
		column_encryption_map;

	for (int i = 0; i < (int) schema_len; ++i) {
		const char *col_name = schema_arr[i];
		parquet::ColumnEncryptionProperties::Builder col_builder(
		    col_name);
#if NNG_ARROW_VERSION_MAJOR >= 22
		col_builder.key(parquet_make_secure_string(conf->encryption.key))
		    ->key_metadata("col_key_metadata");
#else
		col_builder.key(conf->encryption.key)
		    ->key_metadata("col_key_metadata");
#endif
		column_encryption_map[col_name] = col_builder.build();
	}

#if NNG_ARROW_VERSION_MAJOR >= 22
	parquet::FileEncryptionProperties::Builder file_encryption_builder(
	    parquet_make_secure_string(conf->encryption.key));
#else
	parquet::FileEncryptionProperties::Builder file_encryption_builder(
	    conf->encryption.key);
#endif
	auto *enc_builder =
	    file_encryption_builder
	        .footer_key_metadata(conf->encryption.key_id)
	        ->encrypted_columns(column_encryption_map)
	        ->algorithm(static_cast<parquet::ParquetCipher::type>(
	            conf->encryption.type));
	if (conf->encryption.plaintext_footer) {
		enc_builder->set_plaintext_footer();
	}
	encryption_configurations = enc_builder->build();

	return encryption_configurations;
}

void
update_parquet_file_ranges(
    conf_parquet *conf, parquet_object *elem, parquet_file_range *range)
{
	if (conf->file_count == 0) {
		// No file count limit configured, free the range and return
		parquet_file_range_free(range);
		return;
	}
	if (elem->ranges->size < (int) conf->file_count) {
		elem->ranges->range =
		    (parquet_file_range **) realloc(elem->ranges->range,
		        sizeof(parquet_file_range *) * (++elem->ranges->size));
		elem->ranges->range[elem->ranges->size - 1] = range;
	} else {
		// Free old ranges and insert new ranges
		// update start index
		parquet_file_range_free(
		    elem->ranges->range[elem->ranges->start]);
		elem->ranges->range[elem->ranges->start] = range;
		elem->ranges->start++;
		elem->ranges->start %= elem->ranges->size;
	}
}

std::string
compute_and_rename_file_withMD5_CXX(const std::string &filename,
    const conf_parquet *conf, const std::string &topic)
{
	char md5_buffer[MD5_LEN + 1] = { 0 };
	log_debug("Computing MD5...");

	// Step 1: Compute MD5 checksum of the file
	if (ComputeFileMD5(filename.c_str(), md5_buffer) != 0) {
		log_error("Failed to calculate md5sum");
		if (remove(filename.c_str()) != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename.c_str(), errno);
		}
		return {};
	}

	// Step 2: Extract timestamp substring from the original filename
	if (conf == NULL || conf->dir == NULL ||
	    conf->file_name_prefix == NULL) {
		log_error("Invalid parquet conf for rename, keep file %s",
		    filename.c_str());
		return {};
	}
	std::string prefix =
	    std::string(conf->dir) + "/" + conf->file_name_prefix;
	size_t ts_start_pos =
	    prefix.size() + 1; // assumes an extra separator ("/" or "_")
	size_t ts_end_pos = filename.rfind('.');
	if (ts_end_pos == std::string::npos || ts_end_pos <= ts_start_pos) {
		log_error("Invalid filename format: %s", filename.c_str());
		if (remove(filename.c_str()) != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename.c_str(), errno);
		}

		return {};
	}
	std::string timestamp =
	    filename.substr(ts_start_pos, ts_end_pos - ts_start_pos);

	// Step 3: Get queue index for the topic
	uint32_t    index  = file_manager.get_queue_index(topic);
	std::string sindex = std::to_string(index);

	auto        node = file_manager.fetch_conf(topic);
	const char *name = NULL;
	if (node != NULL && node->name != NULL) {
		name = node->name;
	} else if (conf->name != NULL) {
		name = conf->name;
	}
	if (name == NULL) {
		log_error("parquet name is null, skip rename and keep file %s",
		    filename.c_str());
		return {};
	}

	// Step 4: Build new filename:
	// <dir+prefix>_<name>-<timestamp>_<index>_<md5>.parquet
	std::string new_name = prefix + "_" + name + "-" + timestamp + "_" +
	    sindex + "_" + md5_buffer + ".parquet";

	// Step 5: Rename the file to the new name
	log_info(
	    "Trying to rename %s to %s", filename.c_str(), new_name.c_str());
	if (rename(filename.c_str(), new_name.c_str()) != 0) {
		log_error("Failed to rename file %s to %s errno: %d",
		    filename.c_str(), new_name.c_str(), errno);
		if (remove(filename.c_str()) != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename.c_str(), errno);
		}
		return {};
	}

	return new_name;
}

char *
compute_and_rename_file_withMD5(
    const char *filename, const conf_parquet *conf, const char *topic)
{
	std::string result =
	    compute_and_rename_file_withMD5_CXX(filename, conf, topic);
	if (filename) {
		free((void *) filename); // Free the original filename as it's no longer needed
	}
	if (result.empty()) {
		return NULL;
	}

	char *out = (char *) malloc(result.size() + 1);
	if (out)
		strcpy(out, result.c_str());
	return out;
}

static const char *
parquet_cipher_to_str(conf_parquet *conf)
{
	if (conf == NULL || conf->encryption.enable == false) {
		return "NONE";
	}
	switch (conf->encryption.type) {
	case AES_GCM_V1:
		return "AES_GCM_V1";
	case AES_GCM_CTR_V1:
		return "AES_GCM_CTR_V1";
	default:
		return "UNKNOWN";
	}
}

static shared_ptr<arrow::KeyValueMetadata>
parquet_build_nmq_metadata(
    conf_parquet *conf, const char *topic, parquet_type write_type)
{
	shared_ptr<arrow::KeyValueMetadata> kv =
	    make_shared<arrow::KeyValueMetadata>();
	static std::atomic<uint64_t> raw_stream_number { 0 };

	const char *topic_value =
	    topic != NULL ? topic : (conf && conf->name ? conf->name : "");
	const char *key_id =
	    (conf && conf->encryption.key_id) ? conf->encryption.key_id : "";
	const char *wrap_alg    = (conf && conf->encryption.enable)
	    ? PARQUET_WRAP_ALG_NMQ_CONF_CIPHER_AES_GCM_BASE64
	    : "NONE";
	const char *wrapped_key = "";
	if (conf && conf->encryption.enable && conf->encryption.key_cipher) {
		wrapped_key = conf->encryption.key_cipher;
	}

	kv->Append("nmq.meta.version", "1");
	kv->Append("nmq.topic", topic_value);
	kv->Append("nmq.enc.cipher", parquet_cipher_to_str(conf));
	kv->Append("nmq.key.id", key_id);
	kv->Append("nmq.key.wrap_alg", wrap_alg);
	kv->Append("nmq.key.wrapped", wrapped_key);

	kv->Append("nmq.created_by", "NanoMQ");
	kv->Append("nmq.created_at", std::to_string((long long) time(NULL)));

	// Backward compatibility: keep legacy self-increment key for raw stream parquet.
	if (write_type == WRITE_RAW || write_type == WRITE_TEMP_RAW) {
		kv->Append(
		    "number", std::to_string(raw_stream_number.fetch_add(1)));
	}
	return kv;
}

static std::shared_ptr<arrow::DataType>
pq_type_to_arrow(const pq_type *t);

static std::shared_ptr<arrow::Field>
pq_type_to_field(const pq_type *t)
{
	bool nullable = (t->repetition == PQ_OPTIONAL);
	const char *name = t->name != NULL ? t->name : "";
	return arrow::field(name, pq_type_to_arrow(t), nullable);
}

static std::shared_ptr<arrow::DataType>
pq_type_to_arrow(const pq_type *t)
{
	if (t->kind == PQ_PRIMITIVE) {
		switch (t->phys) {
		case PQ_INT32:
			return arrow::int32();
		case PQ_INT64:
			return arrow::uint64();
		case PQ_BYTE_ARRAY:
		default:
			return arrow::binary();
		}
	}
	if (t->kind == PQ_LIST) {
		return arrow::list(pq_type_to_field(&t->children[0]));
	}
	std::vector<std::shared_ptr<arrow::Field>> fields;
	for (uint32_t i = 0; i < t->n_children; i++) {
		fields.push_back(pq_type_to_field(&t->children[i]));
	}
	return arrow::struct_(fields);
}

static std::shared_ptr<arrow::ArrayBuilder>
pq_make_builder(const pq_type *t)
{
	arrow::MemoryPool *pool = arrow::default_memory_pool();
	if (t->kind == PQ_PRIMITIVE) {
		switch (t->phys) {
		case PQ_INT32:
			return std::make_shared<arrow::Int32Builder>(pool);
		case PQ_INT64:
			return std::make_shared<arrow::UInt64Builder>(pool);
		case PQ_BYTE_ARRAY:
		default:
			return std::make_shared<arrow::BinaryBuilder>(pool);
		}
	}
	if (t->kind == PQ_LIST) {
		auto vb        = pq_make_builder(&t->children[0]);
		auto list_type = arrow::list(pq_type_to_field(&t->children[0]));
		return std::make_shared<arrow::ListBuilder>(
		    pool, vb, list_type);
	}
	std::vector<std::shared_ptr<arrow::Field>> fields;
	std::vector<std::shared_ptr<arrow::ArrayBuilder>> builders;
	for (uint32_t i = 0; i < t->n_children; i++) {
		fields.push_back(pq_type_to_field(&t->children[i]));
		builders.push_back(pq_make_builder(&t->children[i]));
	}
	return std::make_shared<arrow::StructBuilder>(
	    arrow::struct_(fields), pool, builders);
}

static arrow::Status
pq_append_one(arrow::ArrayBuilder *b, const pq_array *a, uint32_t i)
{
	bool is_null = (a->valid != NULL && a->valid[i] == 0);
	if (a->type->kind == PQ_PRIMITIVE) {
		if (is_null) {
			return b->AppendNull();
		}
		if (a->type->phys == PQ_INT32) {
			return static_cast<arrow::Int32Builder *>(b)->Append(
			    a->i32[i]);
		}
		if (a->type->phys == PQ_INT64) {
			return static_cast<arrow::UInt64Builder *>(b)->Append(
			    (uint64_t) a->i64[i]);
		}
		if (a->bin == NULL || a->bin[i] == NULL) {
			return b->AppendNull();
		}
		if (a->bin[i]->data == NULL || a->bin[i]->size == 0) {
			return static_cast<arrow::BinaryBuilder *>(b)->Append(
			    "", 0);
		}
		return static_cast<arrow::BinaryBuilder *>(b)->Append(
		    a->bin[i]->data, a->bin[i]->size);
	}
	if (a->type->kind == PQ_LIST) {
		auto *lb = static_cast<arrow::ListBuilder *>(b);
		if (is_null) {
			return lb->AppendNull();
		}
		ARROW_RETURN_NOT_OK(lb->Append());
		uint32_t start = a->offsets[i];
		uint32_t end   = a->offsets[i + 1];
		for (uint32_t j = start; j < end; j++) {
			ARROW_RETURN_NOT_OK(
			    pq_append_one(lb->value_builder(), a->child, j));
		}
		return arrow::Status::OK();
	}
	auto *sb = static_cast<arrow::StructBuilder *>(b);
	if (is_null) {
		return sb->AppendNull();
	}
	ARROW_RETURN_NOT_OK(sb->Append());
	for (uint32_t f = 0; f < a->type->n_children; f++) {
		ARROW_RETURN_NOT_OK(
		    pq_append_one(sb->field_builder(f), a->fields[f], i));
	}
	return arrow::Status::OK();
}

static int
pq_delta_ok(pq_phys phys)
{
	return phys == PQ_INT32 || phys == PQ_INT64;
}

static void
pq_apply_leaf_encoding(parquet::WriterProperties::Builder &builder,
    const pq_type *t, const std::string &path)
{
	if (t->kind == PQ_PRIMITIVE) {
		pq_enc enc = t->enc;
		if (enc == PQ_ENC_ADAPTIVE) {
			/* Unresolved: INT32 dict, INT64 delta, else plain. */
			enc = PQ_ENC_DEFAULT;
		}
		if (enc == PQ_ENC_DEFAULT) {
			if (t->phys == PQ_INT64) {
				enc = PQ_ENC_DELTA;
			} else if (t->phys == PQ_INT32) {
				enc = PQ_ENC_RLE_DICT;
			} else {
				enc = PQ_ENC_PLAIN;
			}
		}
		if (enc == PQ_ENC_RLE_DICT) {
			builder.enable_dictionary(path);
			return;
		}
		parquet::Encoding::type e = Encoding::PLAIN;
		if (enc == PQ_ENC_DELTA && pq_delta_ok(t->phys)) {
			e = Encoding::DELTA_BINARY_PACKED;
		}
		builder.disable_dictionary(path)->encoding(path, e);
		return;
	}
	if (t->kind == PQ_LIST) {
		pq_apply_leaf_encoding(
		    builder, &t->children[0], path + ".list.element");
		return;
	}
	for (uint32_t i = 0; i < t->n_children; i++) {
		std::string next = path.empty()
		    ? std::string(t->children[i].name)
		    : path + "." + t->children[i].name;
		pq_apply_leaf_encoding(builder, &t->children[i], next);
	}
}

static void
pq_collect_leaf_paths(
    const pq_type *t, const std::string &path, std::vector<std::string> &out)
{
	if (t->kind == PQ_PRIMITIVE) {
		out.push_back(path);
		return;
	}
	if (t->kind == PQ_LIST) {
		pq_collect_leaf_paths(
		    &t->children[0], path + ".list.element", out);
		return;
	}
	for (uint32_t i = 0; i < t->n_children; i++) {
		std::string next = path.empty()
		    ? std::string(t->children[i].name)
		    : path + "." + t->children[i].name;
		pq_collect_leaf_paths(&t->children[i], next, out);
	}
}

static void
parquet_release_unused_memory(void)
{
	arrow::MemoryPool *pool = arrow::default_memory_pool();

	if (pool != NULL) {
		pool->ReleaseUnused();
	}
#ifdef __GLIBC__
	malloc_trim(0);
#endif
}

static void
parquet_object_free_and_trim(parquet_object *elem)
{
	parquet_object_free(elem);
	parquet_release_unused_memory();
}

/*
 * Pack pq_array byte-valid into an Arrow LSB bitmap. Empty *out means
 * no nulls (bitmap may be omitted). Keep *out alive while arrays wrap it.
 */
static void
pq_pack_valid_bits(const uint8_t *valid, uint32_t n,
    std::vector<uint8_t> *out, int64_t *null_count)
{
	uint32_t i;
	int64_t  nn = 0;

	*null_count = 0;
	out->clear();
	if (valid == NULL || n == 0) {
		return;
	}
	out->assign((n + 7) / 8, 0);
	for (i = 0; i < n; i++) {
		if (valid[i] != 0) {
			(*out)[i >> 3] |= (uint8_t) (1u << (i & 7));
		} else {
			nn++;
		}
	}
	*null_count = nn;
	if (nn == 0) {
		out->clear();
	}
}

/* Wrap INT32 / UINT64 (PQ_INT64) columns; NULL means use the builder path. */
static std::shared_ptr<arrow::Array>
pq_primitive_as_arrow(const pq_array *col, std::vector<uint8_t> *bitmap_store)
{
	int64_t                         n;
	int64_t                         null_count = 0;
	std::shared_ptr<arrow::Buffer>  nulls;

	if (col == NULL || col->type == NULL ||
	    col->type->kind != PQ_PRIMITIVE) {
		return nullptr;
	}
	if (col->type->phys != PQ_INT32 && col->type->phys != PQ_INT64) {
		return nullptr;
	}
	n = (int64_t) col->length;
	pq_pack_valid_bits(col->valid, col->length, bitmap_store, &null_count);
	if (!bitmap_store->empty()) {
		nulls = arrow::Buffer::Wrap(
		    bitmap_store->data(), (int64_t) bitmap_store->size());
	}
	if (col->type->phys == PQ_INT32) {
		if (n > 0 && col->i32 == NULL) {
			return nullptr;
		}
		return std::make_shared<arrow::Int32Array>(
		    n, arrow::Buffer::Wrap(col->i32, n), nulls, null_count);
	}
	if (n > 0 && col->i64 == NULL) {
		return nullptr;
	}
	return std::make_shared<arrow::UInt64Array>(n,
	    arrow::Buffer::Wrap(
	        reinterpret_cast<const uint64_t *>(col->i64), n),
	    nulls, null_count);
}

static int
parquet_write_nested(conf_parquet *conf, const char *filename,
    parquet_data *data, const char *topic, parquet_type write_type)
{
	if (data->schema_tree == NULL || data->root == NULL ||
	    data->schema_tree->kind != PQ_STRUCT) {
		log_error("nested write requires a STRUCT schema tree");
		return -1;
	}

	try {
		/*
		 * Pick encodings from pq_array values first. Primitive
		 * INT32/UINT64 columns wrap the C buffers (no builder copy);
		 * those arrays must stay alive until WriteTable returns.
		 * LIST/STRUCT/BYTE_ARRAY still copy via builders and can
		 * drop the C column after Finish, except the ts leaf.
		 */
		pq_batch_resolve_adaptive(data);
		std::vector<std::shared_ptr<arrow::Field>> fields;
		std::vector<std::shared_ptr<arrow::Array>> arrays;
		std::vector<std::vector<uint8_t>>          bitmaps(
		            data->schema_tree->n_children);
		for (uint32_t i = 0; i < data->schema_tree->n_children; i++) {
			const pq_type *ct  = &data->schema_tree->children[i];
			pq_array      *col = data->root->fields[i];
			std::shared_ptr<arrow::Array> arr;

			if (col == NULL) {
				log_error("nested missing column %u", i);
				return -1;
			}
			fields.push_back(pq_type_to_field(ct));
			arr = pq_primitive_as_arrow(col, &bitmaps[i]);
			if (arr == nullptr) {
				auto builder = pq_make_builder(ct);
				for (uint32_t r = 0; r < data->row_len; r++) {
					auto st = pq_append_one(
					    builder.get(), col, r);
					if (!st.ok()) {
						log_error(
						    "nested append failed: %s",
						    st.ToString().c_str());
						return -1;
					}
				}
				auto st = builder->Finish(&arr);
				if (!st.ok()) {
					log_error("nested finish failed: %s",
					    st.ToString().c_str());
					return -1;
				}
				if (data->ts == NULL ||
				    (uint64_t *) col->i64 != data->ts) {
					pq_array_free(col);
					data->root->fields[i] = NULL;
				}
			}
			arrays.push_back(arr);
		}

		shared_ptr<arrow::KeyValueMetadata> kv =
		    parquet_build_nmq_metadata(conf, topic, write_type);
		auto arrow_schema = arrow::schema(fields, kv);
		int64_t nrows =
		    arrays.empty() ? 0 : arrays[0]->length();
		auto rb = arrow::RecordBatch::Make(arrow_schema, nrows, arrays);
		if (!rb) {
			log_error("nested RecordBatch::Make failed");
			return -1;
		}
		auto maybe_table = arrow::Table::FromRecordBatches({ rb });
		if (!maybe_table.ok()) {
			log_error("nested FromRecordBatches: %s",
			    maybe_table.status().ToString().c_str());
			return -1;
		}
		auto table = *maybe_table;
		auto vst   = table->ValidateFull();
		if (!vst.ok()) {
			log_error("nested table invalid: %s",
			    vst.ToString().c_str());
			return -1;
		}
		if (table->num_rows() <= 0) {
			log_error("nested table has 0 rows");
			return -1;
		}

		parquet::WriterProperties::Builder props_builder;
		props_builder.created_by("NanoMQ")
		    ->version(parquet::ParquetVersion::PARQUET_2_6)
		    ->data_page_version(parquet::ParquetDataPageVersion::V2)
		    ->encoding(parquet::Encoding::PLAIN)
		    ->compression(static_cast<arrow::Compression::type>(
		        conf->comp_type));
		if (conf->dictionary) {
			props_builder.enable_dictionary();
		} else {
			props_builder.disable_dictionary();
		}
		if (conf->compression_level > 0 &&
		    (conf->comp_type == GZIP || conf->comp_type == BROTLI ||
		        conf->comp_type == ZSTD)) {
			props_builder.compression_level(
			    conf->compression_level);
		} else if (conf->compression_level > 0) {
			log_warn("compression_level is ignored for current "
			         "compress type");
		}
		if (conf->data_page_size > 0 &&
		    conf->data_page_size <= (uint64_t) INT64_MAX) {
			props_builder.data_pagesize(
			    (int64_t) conf->data_page_size);
		}
		if (conf->dictionary_page_size > 0 &&
		    conf->dictionary_page_size <= (uint64_t) INT64_MAX) {
			props_builder.dictionary_pagesize_limit(
			    (int64_t) conf->dictionary_page_size);
		}
		if (conf->write_batch_size > 0) {
			props_builder.write_batch_size(
			    (int64_t) conf->write_batch_size);
		}
		if (conf->enable_statistics) {
			props_builder.enable_statistics();
		} else {
			props_builder.disable_statistics();
		}
		if (conf->enable_page_checksum) {
			props_builder.enable_page_checksum();
		} else {
			props_builder.disable_page_checksum();
		}
		pq_apply_leaf_encoding(props_builder, data->schema_tree, "");
		if (conf->encryption.enable) {
			std::vector<std::string> paths;
			pq_collect_leaf_paths(data->schema_tree, "", paths);
			std::vector<char *> names;
			for (auto &p : paths) {
				names.push_back(const_cast<char *>(p.c_str()));
			}
			props_builder.encryption(parquet_set_encryption(
			    names.data(), (uint32_t) names.size(), conf));
		}
		shared_ptr<parquet::WriterProperties> props =
		    props_builder.build();

		using FileClass = arrow::io::FileOutputStream;
		shared_ptr<FileClass> out_file;
		PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(filename));
		auto st = parquet::arrow::WriteTable(*table,
		    arrow::default_memory_pool(), out_file, table->num_rows(),
		    props);
		if (!st.ok()) {
			log_error("nested WriteTable failed: %s",
			    st.ToString().c_str());
			return -1;
		}
	} catch (const exception &e) {
		log_error("nested write exception=[%s]", e.what());
		return -1;
	}
	parquet_release_unused_memory();
	return 0;
}

static pq_type *
pq_type_from_arrow_field(const arrow::Field &field);

static pq_type *
pq_type_from_arrow(const char *name, const arrow::DataType &dt, bool nullable)
{
	pq_rep rep = nullable ? PQ_OPTIONAL : PQ_REQUIRED;
	switch (dt.id()) {
	case arrow::Type::INT32:
		return pq_type_primitive(name, PQ_INT32, rep, PQ_ENC_DEFAULT);
	case arrow::Type::INT64:
	case arrow::Type::UINT64:
		return pq_type_primitive(name, PQ_INT64, rep, PQ_ENC_DEFAULT);
	case arrow::Type::BINARY:
	case arrow::Type::LARGE_BINARY:
	case arrow::Type::STRING:
	case arrow::Type::LARGE_STRING:
		return pq_type_primitive(
		    name, PQ_BYTE_ARRAY, rep, PQ_ENC_DEFAULT);
	case arrow::Type::LIST: {
		const auto &lt = static_cast<const arrow::ListType &>(dt);
		if (!lt.value_field()) {
			return NULL;
		}
		pq_type *elem = pq_type_from_arrow_field(*lt.value_field());
		return pq_type_list(name, rep, elem);
	}
	case arrow::Type::STRUCT: {
		const auto &st = static_cast<const arrow::StructType &>(dt);
		uint32_t    n  = (uint32_t) st.num_fields();
		std::vector<pq_type *> fields(n);
		for (uint32_t i = 0; i < n; i++) {
			fields[i] = pq_type_from_arrow_field(*st.field(i));
			if (fields[i] == NULL) {
				for (uint32_t j = 0; j < i; j++) {
					pq_type_free(fields[j]);
				}
				return NULL;
			}
		}
		return pq_type_struct(name, rep, fields.data(), n);
	}
	default:
		log_error("unsupported arrow type %s", dt.ToString().c_str());
		return NULL;
	}
}

static pq_type *
pq_type_from_arrow_field(const arrow::Field &field)
{
	return pq_type_from_arrow(
	    field.name().c_str(), *field.type(), field.nullable());
}

static void
pq_copy_validity(pq_array *a, const arrow::Array &arr)
{
	if (arr.null_count() == 0) {
		return;
	}
	if (a->valid == NULL && a->length > 0) {
		a->valid = (uint8_t *) nng_alloc(a->length);
		if (a->valid == NULL) {
			return;
		}
		memset(a->valid, 1, a->length);
	}
	for (int64_t i = 0; i < arr.length(); i++) {
		if (arr.IsNull(i) && a->valid != NULL) {
			a->valid[i] = 0;
		}
	}
}

static pq_array *pq_array_from_arrow(const pq_type *t, const arrow::Array &arr);

static parquet_data_packet *
pq_packet_from_bytes(const uint8_t *p, int32_t n)
{
	if (n < 0) {
		n = 0;
	}
	return pq_packet_copy(p, (uint32_t) n);
}

static pq_array *
pq_array_from_arrow(const pq_type *t, const arrow::Array &arr)
{
	pq_array *a;

	if (t == NULL) {
		return NULL;
	}
	a = pq_array_from_type(t, (uint32_t) arr.length());
	if (a == NULL) {
		return NULL;
	}
	pq_copy_validity(a, arr);
	if (t->kind == PQ_PRIMITIVE) {
		if (t->phys == PQ_INT32) {
			if (arr.type_id() != arrow::Type::INT32) {
				pq_array_free(a);
				return NULL;
			}
			const auto &ia =
			    static_cast<const arrow::Int32Array &>(arr);
			for (int64_t i = 0; i < arr.length(); i++) {
				if (!arr.IsNull(i)) {
					a->i32[i] = ia.Value(i);
				}
			}
		} else if (t->phys == PQ_INT64) {
			if (arr.type_id() == arrow::Type::UINT64) {
				const auto &ua =
				    static_cast<const arrow::UInt64Array &>(
				        arr);
				for (int64_t i = 0; i < arr.length(); i++) {
					if (!arr.IsNull(i)) {
						a->i64[i] = (int64_t) ua.Value(i);
					}
				}
			} else {
				const auto &ia =
				    static_cast<const arrow::Int64Array &>(
				        arr);
				for (int64_t i = 0; i < arr.length(); i++) {
					if (!arr.IsNull(i)) {
						a->i64[i] = ia.Value(i);
					}
				}
			}
		} else {
			const auto &ba =
			    static_cast<const arrow::BinaryArray &>(arr);
			for (int64_t i = 0; i < arr.length(); i++) {
				if (arr.IsNull(i)) {
					continue;
				}
				int32_t     len = 0;
				const uint8_t *p = ba.GetValue(i, &len);
				a->bin[i]        = pq_packet_from_bytes(p, len);
			}
		}
		return a;
	}
	if (t->kind == PQ_LIST) {
		if (arr.type_id() != arrow::Type::LIST) {
			pq_array_free(a);
			return NULL;
		}
		const auto &la = static_cast<const arrow::ListArray &>(arr);
		if (arr.length() == 0) {
			return a;
		}
		int32_t base = la.value_offset(0);
		int32_t last = la.value_offset(arr.length());
		if (last < base) {
			pq_array_free(a);
			return NULL;
		}
		for (int64_t i = 0; i <= arr.length(); i++) {
			a->offsets[i] =
			    (uint32_t) (la.value_offset(i) - base);
		}
		auto values = la.values()->Slice(base, last - base);
		pq_array_free(a->child);
		a->child = pq_array_from_arrow(&t->children[0], *values);
		if (a->child == NULL) {
			pq_array_free(a);
			return NULL;
		}
		return a;
	}
	if (arr.type_id() != arrow::Type::STRUCT) {
		return NULL;
	}
	const auto &sa = static_cast<const arrow::StructArray &>(arr);
	for (uint32_t i = 0; i < t->n_children; i++) {
		auto field_arr = sa.field(i);
		pq_array_free(a->fields[i]);
		a->fields[i] =
		    pq_array_from_arrow(&t->children[i], *field_arr);
		if (a->fields[i] == NULL) {
			pq_array_free(a);
			return NULL;
		}
	}
	return a;
}

static std::shared_ptr<arrow::Array>
pq_chunked_to_array(const std::shared_ptr<arrow::ChunkedArray> &ch)
{
	if (ch->num_chunks() == 1) {
		return ch->chunk(0);
	}
	auto maybe =
	    arrow::Concatenate(ch->chunks(), arrow::default_memory_pool());
	if (!maybe.ok()) {
		log_error("concatenate chunks: %s",
		    maybe.status().ToString().c_str());
		return nullptr;
	}
	return *maybe;
}

static bool
parquet_file_schema_is_nested(const parquet::SchemaDescriptor *sd)
{
	if (sd == NULL) {
		return false;
	}
	for (int i = 0; i < sd->num_columns(); i++) {
		const parquet::ColumnDescriptor *col = sd->Column(i);
		if (col->max_repetition_level() > 0) {
			return true;
		}
		if (col->physical_type() == parquet::Type::INT32) {
			return true;
		}
	}
	return false;
}

static shared_ptr<GroupNode>
setup_schema_from_data(parquet_data *data)
{
	if (data->schema != NULL && data->col_len > 0) {
		return setup_schema(data->schema, data->col_len);
	}
	if (data->schema_tree == NULL ||
	    data->schema_tree->kind != PQ_STRUCT ||
	    data->schema_tree->n_children == 0) {
		return NULL;
	}
	std::vector<char *> names(data->schema_tree->n_children);
	for (uint32_t i = 0; i < data->schema_tree->n_children; i++) {
		names[i] = data->schema_tree->children[i].name;
	}
	return setup_schema(names.data(), data->schema_tree->n_children);
}

static parquet_data_packet *
pq_flat_cell(parquet_data *data, uint32_t c, uint32_t r)
{
	if (data->payload_arr != NULL && data->payload_arr[c] != NULL) {
		return data->payload_arr[c][r];
	}
	if (data->root != NULL && data->root->fields != NULL &&
	    c + 1 < data->root->type->n_children) {
		pq_array *f = data->root->fields[c + 1];
		if (f->valid != NULL && f->valid[r] == 0) {
			return NULL;
		}
		if (f->bin != NULL) {
			return f->bin[r];
		}
	}
	return NULL;
}

int
parquet_write_core(conf_parquet *conf, char *filename,
    shared_ptr<GroupNode> schema, parquet_data *data, const char *topic,
    parquet_type write_type)
{

	char                 **schema_arr  = data->schema;
	uint32_t               col_len     = data->col_len;
	uint32_t               row_len     = data->row_len;
	uint64_t              *ts_arr      = data->ts;
	const char            *ts_col      = "ts";

	if (col_len == 0 && data->schema_tree != NULL) {
		col_len = data->schema_tree->n_children;
	}
	if (schema_arr != NULL && schema_arr[0] != NULL) {
		ts_col = schema_arr[0];
	} else if (data->schema_tree != NULL &&
	    data->schema_tree->n_children > 0 &&
	    data->schema_tree->children[0].name != NULL) {
		ts_col = data->schema_tree->children[0].name;
	}

	string exception_msg = "";
	try {

		parquet::WriterProperties::Builder builder;
		log_debug("init builder");
		builder.created_by("NanoMQ")
		    ->version(parquet::ParquetVersion::PARQUET_2_6)
		    ->data_page_version(parquet::ParquetDataPageVersion::V2)
		    ->encoding(parquet::Encoding::PLAIN)
		    ->encoding(ts_col, Encoding::DELTA_BINARY_PACKED)
		    ->compression(static_cast<arrow::Compression::type>(
		        conf->comp_type));
		if (conf->dictionary) {
			builder.enable_dictionary();
		} else {
			builder.disable_dictionary();
		}
		if (conf->compression_level > 0 &&
		    (conf->comp_type == GZIP || conf->comp_type == BROTLI ||
		        conf->comp_type == ZSTD)) {
			builder.compression_level(conf->compression_level);
		} else if (conf->compression_level > 0) {
			log_warn("compression_level is ignored for current "
			         "compress type");
		}
		if (conf->data_page_size > 0) {
			if (conf->data_page_size >
			    (uint64_t) INT64_MAX) {
				log_warn("data_page_size %" PRIu64
				         " exceeds INT64_MAX, ignored",
				    conf->data_page_size);
			} else {
				builder.data_pagesize(
				    (int64_t) conf->data_page_size);
			}
		}
		if (conf->dictionary_page_size > 0) {
			if (conf->dictionary_page_size >
			    (uint64_t) INT64_MAX) {
				log_warn("dictionary_page_size %" PRIu64
				         " exceeds INT64_MAX, ignored",
				    conf->dictionary_page_size);
			} else {
				builder.dictionary_pagesize_limit(
				    (int64_t) conf->dictionary_page_size);
			}
		}
		if (conf->write_batch_size > 0) {
			builder.write_batch_size(
			    (int64_t) conf->write_batch_size);
		}
		if (conf->enable_statistics) {
			builder.enable_statistics();
		} else {
			builder.disable_statistics();
		}
		if (conf->enable_page_checksum) {
			builder.enable_page_checksum();
		} else {
			builder.disable_page_checksum();
		}
		if (data->schema_tree != NULL) {
			pq_apply_leaf_encoding(
			    builder, data->schema_tree, "");
		}
		log_debug("check encry");
		if (conf->encryption.enable) {
			shared_ptr<parquet::FileEncryptionProperties>
			    encryption_configurations;
			if (schema_arr != NULL) {
				encryption_configurations =
				    parquet_set_encryption(
				        schema_arr, col_len, conf);
			} else {
				std::vector<std::string> paths;
				pq_collect_leaf_paths(
				    data->schema_tree, "", paths);
				std::vector<char *> names;
				for (auto &p : paths) {
					names.push_back(
					    const_cast<char *>(p.c_str()));
				}
				encryption_configurations =
				    parquet_set_encryption(names.data(),
				        (uint32_t) names.size(), conf);
			}
			builder.encryption(encryption_configurations);
		}

		shared_ptr<parquet::WriterProperties> props = builder.build();
		using FileClass = arrow::io::FileOutputStream;
		shared_ptr<FileClass> out_file;
		PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(filename));
		shared_ptr<parquet::ParquetFileWriter> file_writer =
		    parquet::ParquetFileWriter::Open(out_file, schema, props);

		shared_ptr<arrow::KeyValueMetadata> key_value_metadata =
		    parquet_build_nmq_metadata(conf, topic, write_type);
		file_writer->AddKeyValueMetadata(key_value_metadata);

		// Append a RowGroup with a specific number of rows.
		parquet::RowGroupWriter *rg_writer =
		    file_writer->AppendRowGroup();

		// Write the Int64 column
		log_debug("start doing int64 write");
		parquet::Int64Writer *int64_writer =
		    static_cast<parquet::Int64Writer *>(
		        rg_writer->NextColumn());
		for (uint32_t r = 0; r < row_len; r++) {
			int64_t value            = ts_arr[r];
			int16_t definition_level = 1;
			int64_writer->WriteBatch(
			    1, &definition_level, nullptr, &value);
		}
		log_debug("stop doing int64 write");

		// Write the ByteArray column. Make every alternate values NULL
		for (uint32_t c = 0; c < col_len - 1; c++) {
			parquet::ByteArrayWriter *ba_writer =
			    static_cast<parquet::ByteArrayWriter *>(
			        rg_writer->NextColumn());
			for (uint32_t r = 0; r < row_len; r++) {
				parquet_data_packet *pkt =
				    pq_flat_cell(data, c, r);
				if (pkt != NULL) {
					int16_t definition_level = 1;
					parquet::ByteArray value;
					value.ptr = pkt->data;
					value.len = pkt->size;
					ba_writer->WriteBatch(1,
					    &definition_level, nullptr,
					    &value);
				} else {
					int16_t definition_level = 0;
					ba_writer->WriteBatch(1,
					    &definition_level, nullptr,
					    nullptr);
				}
			}
		}
		// Close the RowGroupWriter
		rg_writer->Close();
		// Close the ParquetFileWriter
		file_writer->Close();
		log_debug("stop doing ByteArray write");

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return 0;
}

int
parquet_write_file(conf_parquet *conf, const char *filename, parquet_data *data,
    const char *topic, parquet_type write_type)
{
	if (conf == NULL || filename == NULL || data == NULL ||
	    data->row_len == 0) {
		log_error("parquet_write_file invalid args");
		return -1;
	}
	if (data->ts == NULL) {
		pq_batch_bind_ts(data);
	}
	if (data->ts == NULL) {
		log_error("parquet_write_file missing ts column");
		return -1;
	}
	if (data->root != NULL && !pq_batch_is_flat_ba(data)) {
		return parquet_write_nested(
		    conf, filename, data, topic, write_type);
	}
	shared_ptr<GroupNode> schema = setup_schema_from_data(data);
	if (schema == NULL) {
		log_error("Schema set error.");
		return -1;
	}
	return parquet_write_core(
	    conf, (char *) filename, schema, data, topic, write_type);
}

int
parquet_read_file(
    conf_parquet *conf, const char *filename, parquet_data **out)
{
	if (filename == NULL || out == NULL) {
		return -1;
	}
	*out = NULL;

	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();
	bool is_compat_mode = false;
	bool is_encrypted   = false;
	if (0 != parquet_check_is_compat_and_decrypt(
	             (char *) filename, is_compat_mode, is_encrypted)) {
		log_warn("failed to check mode for parquet %s", filename);
		return -1;
	}
	if (is_compat_mode == false && is_encrypted == true) {
		if (false == parquet_resolve_and_set_decryption_properties(
		                 reader_properties, conf, filename)) {
			log_error("Can't read encrypted parquet");
			return -1;
		}
	}

	try {
		parquet::arrow::FileReaderBuilder frb;
		auto st = frb.OpenFile(filename, false, reader_properties);
		if (!st.ok()) {
			log_error("OpenFile %s: %s", filename,
			    st.ToString().c_str());
			return -1;
		}
		std::unique_ptr<parquet::arrow::FileReader> reader;
		st = frb.Build(&reader);
		if (!st.ok()) {
			log_error("FileReader build: %s",
			    st.ToString().c_str());
			return -1;
		}
		std::shared_ptr<arrow::Table> table;
		st = reader->ReadTable(&table);
		if (!st.ok()) {
			log_error("ReadTable: %s", st.ToString().c_str());
			return -1;
		}

		uint32_t n = (uint32_t) table->num_columns();
		std::vector<pq_type *> fields(n);
		for (uint32_t i = 0; i < n; i++) {
			fields[i] =
			    pq_type_from_arrow_field(*table->schema()->field(i));
			if (fields[i] == NULL) {
				for (uint32_t j = 0; j < i; j++) {
					pq_type_free(fields[j]);
				}
				return -1;
			}
		}
		pq_type *root_t =
		    pq_type_struct("schema", PQ_REQUIRED, fields.data(), n);
		if (root_t == NULL) {
			return -1;
		}
		parquet_data *data = pq_batch_from_type(
		    root_t, (uint32_t) table->num_rows());
		if (data == NULL) {
			return -1;
		}
		pq_array_free(data->root);
		data->root = pq_array_from_type(root_t, (uint32_t) table->num_rows());
		if (data->root == NULL) {
			parquet_data_free(data);
			return -1;
		}
		for (uint32_t i = 0; i < n; i++) {
			auto arr = pq_chunked_to_array(table->column(i));
			if (arr == NULL) {
				parquet_data_free(data);
				return -1;
			}
			pq_array_free(data->root->fields[i]);
			data->root->fields[i] = pq_array_from_arrow(
			    &root_t->children[i], *arr);
			if (data->root->fields[i] == NULL) {
				parquet_data_free(data);
				return -1;
			}
		}
		pq_batch_bind_ts(data);
		*out = data;
		return 0;
	} catch (const exception &e) {
		log_error("parquet_read_file exception=[%s]", e.what());
		return -1;
	}
}

int
parquet_file_num_row_groups(const char *filename)
{
	if (filename == NULL) {
		return -1;
	}
	try {
		unique_ptr<parquet::ParquetFileReader> reader =
		    parquet::ParquetFileReader::OpenFile(filename, false);
		if (!reader || !reader->metadata()) {
			return -1;
		}
		return reader->metadata()->num_row_groups();
	} catch (const exception &e) {
		log_error("num_row_groups %s: %s", filename, e.what());
		return -1;
	}
}

int
parquet_write_tmp(parquet_object *elem)
{

	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	uint32_t  row_len = elem->data->row_len;
	uint64_t *ts_arr  = elem->data->ts;
	if (ts_arr == NULL) {
		pq_batch_bind_ts(elem->data);
		ts_arr = elem->data->ts;
	}
	if (ts_arr == NULL) {
		log_error("Schema set error.");
		return -1;
	}

	log_debug("parquet_write");

	string prefix  = gen_random(6);
	prefix         = "nanomq" + prefix;
	char *filename = get_random_file_name(
	    conf, prefix.data(), ts_arr[0], ts_arr[row_len - 1]);
	if (filename == NULL) {
		log_error("Failed to get file name");
		parquet_object_free_and_trim(elem);
		return -1;
	}

	parquet_write_file(
	    conf, filename, elem->data, elem->topic, elem->type);
	parquet_file_range *range =
	    parquet_file_range_alloc(0, row_len - 1, filename);
	free(filename);
	update_parquet_file_ranges(conf, elem, range);

	parquet_object_free_and_trim(elem);
	return 0;
}

int
parquet_write(parquet_object *elem)
{

	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	uint32_t  row_len = elem->data->row_len;
	uint64_t *ts_arr  = elem->data->ts;
	if (ts_arr == NULL) {
		pq_batch_bind_ts(elem->data);
		ts_arr = elem->data->ts;
	}
	if (ts_arr == NULL) {
		log_error("Schema set error.");
		return -1;
	}

	log_debug("parquet_write");
	char *filename = get_file_name(conf, ts_arr[0], ts_arr[row_len - 1]);
	if (filename == NULL) {
		parquet_object_free_and_trim(elem);
		log_error("Failed to get file name");
		return -1;
	}

	if (parquet_write_file(
	        conf, filename, elem->data, elem->topic, elem->type) != 0) {
		parquet_object_free_and_trim(elem);
		log_error("parquet_write_file failed");
		return -1;
	}
	char *md5_file_name =
	    compute_and_rename_file_withMD5(filename, conf, elem->topic);
	if (md5_file_name == nullptr) {
		parquet_object_free_and_trim(elem);
		log_error("fail to get md5 from parquet file");
		return -1;
	}

	char md5_buffer[MD5_LEN + 1];
	log_debug("compute md5 after rename");
	int ret = ComputeFileMD5(md5_file_name, md5_buffer);
	if (ret != 0) {
		log_error("Failed to calculate md5sum");
	}

	parquet_file_range *range = parquet_file_range_alloc(
	    0, elem->data->row_len - 1, md5_file_name);
	update_parquet_file_ranges(conf, elem, range);

	log_debug("wait for parquet_queue_mutex");
	pthread_mutex_lock(&parquet_queue_mutex);
	file_manager.update_queue(elem->topic, md5_file_name);
	pthread_mutex_unlock(&parquet_queue_mutex);

	log_info("flush finished!");
	parquet_object_free_and_trim(elem);
	return 0;
}

void *
parquet_write_loop_v2(void *arg)
{
	(void(arg));

	while (true) {
		// wait for mqtt messages to send method request
		pthread_mutex_lock(&parquet_queue_mutex);

		while (IS_EMPTY(parquet_queue)) {
			pthread_cond_wait(
			    &parquet_queue_not_empty, &parquet_queue_mutex);
		}

		log_debug("fetch element from parquet queue");
		parquet_object *ele =
		    (parquet_object *) DEQUEUE(parquet_queue);

		pthread_mutex_unlock(&parquet_queue_mutex);

		switch (ele->type) {
		case WRITE_RAW:
		case WRITE_CAN:
			parquet_write(ele);
			break;
		case WRITE_TEMP_RAW:
			parquet_write_tmp(ele);
			break;
		default:
			break;
		}
	}
	return NULL;
}


int
parquet_write_launcher(conf_exchange *conf)
{
	INIT_QUEUE(parquet_queue);

	for (size_t i = 0; i < conf->count; i++) {
		file_manager.add_queue(conf->nodes[i]);
	}

	is_available = true;
	pthread_t write_thread;
	int       result =
	    pthread_create(&write_thread, NULL, parquet_write_loop_v2, conf);
	if (result != 0) {
		log_error("Failed to create parquet write thread.");
		return -1;
	}

	return 0;
}

static void
get_range(const char *name, uint64_t range[2])
{
	// {prefix}_{topic}-{start_ts}~{end_ts}_{md5}.parquet
	const char *ts_start = strrchr(name, '-');
	if (!ts_start) {
		range[0] = range[1] = 0;
		return;
	}
	ts_start++;

	char md5[33] = { 0 };

	if (sscanf(ts_start, "%lu~%lu_%32[^.]", &range[0], &range[1], md5) !=
	    3) {
		range[0] = range[1] = 0;
	}
}

static bool
compare_callback(void *name, uint64_t key)
{
	uint64_t range[2] = { 0 };
	get_range((const char *) name, range);
	return (key >= range[0] && key <= range[1]);
}

static bool
compare_callback_span(void *name, uint64_t low, uint64_t high)
{
	uint64_t range[2] = { 0 };
	get_range((const char *) name, range);
	return !(low > range[1] || high < range[0]);
}

const char *
parquet_find(const char *topic, uint64_t key)
{
	conf_parquet *conf = file_manager.fetch_conf(topic);
	if (conf == NULL || conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic);
		return NULL;
	}

	WAIT_FOR_AVAILABLE
	const char *value = NULL;
	void       *elem  = NULL;
	pthread_mutex_lock(&parquet_queue_mutex);
	auto queue = file_manager.fetch_queue(topic);
	if (queue == NULL) {
		pthread_mutex_unlock(&parquet_queue_mutex);
		return NULL;
	}
	FOREACH_QUEUE(*queue, elem)
	{
		if (elem) {
			if (compare_callback(elem, key)) {
				value = nng_strdup((char *) elem);
				goto found;
			}
		}
	}
found:
	pthread_mutex_unlock(&parquet_queue_mutex);
	return value;
}

const char **
parquet_find_span(
    const char *topic, uint64_t start_key, uint64_t end_key, uint32_t *size)
{

	if (start_key > end_key) {
		log_error("Start key can't be greater than end_key.");
		*size = 0;
		return NULL;
	}
	WAIT_FOR_AVAILABLE
	uint64_t     low        = start_key;
	uint64_t     high       = end_key;
	uint32_t     local_size = 0;
	const char  *value      = NULL;
	const char **array      = NULL;
	const char **ret        = NULL;
	void        *elem       = NULL;

	pthread_mutex_lock(&parquet_queue_mutex);

	auto queue = file_manager.fetch_queue(topic);
	if (queue == NULL) {
		pthread_mutex_unlock(&parquet_queue_mutex);
		*size = 0;
		return NULL;
	}
	if (queue->size != 0) {
		array = (const char **) nng_alloc(sizeof(char *) * queue->size);

		ret = array;
		FOREACH_QUEUE(*queue, elem)
		{
			if (elem) {
				if (compare_callback_span(elem, low, high)) {
					++local_size;
					value    = nng_strdup((char *) elem);
					*array++ = value;
				}
			}
		}
	}

	pthread_mutex_unlock(&parquet_queue_mutex);
	(*size) = local_size;
	return ret;
}

bool
parquet_read_set_property(
    parquet::ReaderProperties &reader_properties, const char *key)
{
	if (key != NULL && strlen(key) > 0) {
		parquet::FileDecryptionProperties::Builder builder;
#if NNG_ARROW_VERSION_MAJOR >= 22
		shared_ptr<parquet::FileDecryptionProperties>
		    decryption_configuration =
		        builder.footer_key(parquet_make_secure_string(key))
		            ->key_retriever(
		                std::make_shared<UniformKeyRetriever>(key))
		            ->build();
		reader_properties.file_decryption_properties(
		    decryption_configuration);
#else
		shared_ptr<parquet::FileDecryptionProperties>
		    decryption_configuration =
		        builder.footer_key(key)
		            ->key_retriever(
		                std::make_shared<UniformKeyRetriever>(key))
		            ->build();
		// Arrow <22 requires a deep clone before attaching to reader props.
		reader_properties.file_decryption_properties(
		    decryption_configuration->DeepClone());
#endif
		return true;
	}
	return false;
}

// Return 0 when no exception happened.
static int
parquet_check_is_compat_and_decrypt(
		char *filename, bool &is_compat_mode, bool &is_encrypted)
{
	string exception_msg = "";
	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	// Access MetaData and decide if we need a decryption_configuration
	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();
		auto kv = file_metadata->key_value_metadata();
		if (kv == nullptr) {
			is_compat_mode = true;
			is_encrypted   = false;
		} else {
			int idx;
			if (((idx = kv->FindKey("nmq.created_by")) >= 0) &&
			     (kv->value(idx).compare("NanoMQ") == 0)) {
				// Yes. It's a parquet file owned by NanoMQ
				if (((idx = kv->FindKey("nmq.key.wrap_alg")) >= 0) &&
				     (kv->value(idx).compare(PARQUET_WRAP_ALG_NMQ_CONF_CIPHER_AES_GCM_BASE64) == 0) &&
					((idx = kv->FindKey("nmq.key.wrapped")) >= 0) &&
					 (kv->value(idx).length() > 0)) {
					is_compat_mode = false;
					is_encrypted = true;
				}
			} else {
				is_compat_mode = true;
				is_encrypted   = false;
			}
		}
	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("access metadata exception_msg=[%s]", exception_msg.c_str());
		return -1;
	}
	return 0;
}

static uint8_t *
parquet_read(conf_parquet *conf, char *filename, uint64_t key, uint32_t *len)
{
	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	string exception_msg = "";
	bool is_compat_mode = false;
	bool is_encrypted = false;

	if (0 != parquet_check_is_compat_and_decrypt(
				filename, is_compat_mode, is_encrypted)) {
		log_warn("failed to check mode and encryption for parquet %s, skip", filename);
		return NULL;
	}

	if (is_compat_mode == false && is_encrypted == true) {
		log_info("parquet mode [v1] [encrypted]: %s", filename);
		if (false == parquet_resolve_and_set_decryption_properties(
						reader_properties, conf, filename)) {
			log_error("Can't read encrypted parquet due to no encryption config");
			return NULL;
		}
	} else if (is_compat_mode == false && is_encrypted == false) {
		log_info("parquet mode [v1]: %s", filename);
	} else {
		log_info("parquet mode [compat]: %s", filename);
	}

	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();

		int num_row_groups =
		    file_metadata
		        ->num_row_groups(); // Get the number of RowGroups
		for (int r = 0; r < num_row_groups; ++r) {

			shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			int64_t values_read = 0;
			int64_t rows_read   = 0;
			int16_t definition_level;
			int16_t repetition_level;
			shared_ptr<parquet::ColumnReader> column_reader;

			// Get the Column Reader for the Int64 column
			column_reader = row_group_reader->Column(0);
			parquet::Int64Reader *int64_reader =
			    static_cast<parquet::Int64Reader *>(
			        column_reader.get());

			int i = 0;
			while (int64_reader->HasNext()) {
				int64_t value;
				rows_read = int64_reader->ReadBatch(1,
				    &definition_level, &repetition_level,
				    &value, &values_read);
				if (1 == rows_read && 1 == values_read) {
					if (((uint64_t) value) == key)
						break;
				}
				i++;
			}

			// Get the Column Reader for the ByteArray column
			column_reader = row_group_reader->Column(1);
			parquet::ByteArrayReader *ba_reader =
			    static_cast<parquet::ByteArrayReader *>(
			        column_reader.get());

			if (ba_reader->HasNext()) {
				ba_reader->Skip(i);
			}

			if (ba_reader->HasNext()) {
				parquet::ByteArray value;
				rows_read =
				    ba_reader->ReadBatch(1, &definition_level,
				        nullptr, &value, &values_read);
				if (1 == rows_read && 1 == values_read) {
					uint8_t *ret = (uint8_t *) malloc(
					    value.len * sizeof(uint8_t));
					memcpy(ret, value.ptr, value.len);
					*len = value.len;
					return ret;
				}
			}
		}

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return NULL;
}

static vector<int>
get_keys_indexes(
    parquet::Int64Reader *int64_reader, const vector<uint64_t> &keys)
{
	vector<int> index_vector;
	int64_t     values_read = 0;
	int64_t     rows_read   = 0;
	int16_t     definition_level;
	int16_t     repetition_level;

	int index = 0;
	for (const auto &key : keys) {
		bool found = false;
		while (int64_reader->HasNext()) {
			int64_t value;
			rows_read =
			    int64_reader->ReadBatch(1, &definition_level,
			        &repetition_level, &value, &values_read);
			if (1 == rows_read && 1 == values_read) {
				if (((uint64_t) value) == key) {
					index_vector.push_back(index++);
					found = true;
					break;
				}
			}
			index++;
		}
		if (!found) {
			index_vector.push_back(-1);
		}
	}

	return index_vector;
}

static vector<int>
get_keys_indexes_fuzing(
    parquet::Int64Reader *int64_reader, vector<uint64_t> &ts, uint64_t start_key, uint64_t end_key)
{
	vector<int> index_vector;
	int64_t     values_read = 0;
	int64_t     rows_read   = 0;
	int16_t     definition_level;
	int16_t     repetition_level;

	int  index = 0;
	bool found = false;

	log_debug("start_key: %lu, end_key: %lu", start_key, end_key);

	while (int64_reader->HasNext()) {
		int64_t value;
		rows_read = int64_reader->ReadBatch(1, &definition_level,
		    &repetition_level, &value, &values_read);
		if (1 == rows_read && 1 == values_read) {
			log_trace("read value: %lu", value);
			if (((uint64_t) value) >= start_key && ((uint64_t) value) <= end_key) {
				index_vector.push_back(index++);
				ts.push_back(value);
				found = true;
				break;
			}
			index++;
		}
	}
	if (!found) {
		index_vector.push_back(-1);
		index_vector.push_back(-1);
	} else {
		found = false;
		while (int64_reader->HasNext()) {
			int64_t value;
			rows_read =
			    int64_reader->ReadBatch(1, &definition_level,
			        &repetition_level, &value, &values_read);
			log_trace("read value: %lu", value);
			if (1 == rows_read && 1 == values_read) {
				if (((uint64_t) value) > end_key) {
					index_vector.push_back(index - 1);
					found = true;
					break;
				} else if (((uint64_t) value) == end_key) {
					index_vector.push_back(index);
					ts.push_back(value);
					found = true;
					break;
				} else {
					ts.push_back(value);
				}
				index++;
			}
		}

		if (!found) {
			index_vector.push_back(index - 1);
		}
	}

	return index_vector;
}

static vector<parquet_data_packet *>
parquet_read(conf_parquet *conf, char *filename, vector<uint64_t> keys)
{
	vector<parquet_data_packet *> ret_vec;
	string                   path_int64 = "key";
	string                   path_str   = "data";
	parquet::ReaderProperties     reader_properties =
	    parquet::default_reader_properties();

	string exception_msg = "";
	bool is_compat_mode = false;
	bool is_encrypted = false;

	if (0 != parquet_check_is_compat_and_decrypt(
				filename, is_compat_mode, is_encrypted)) {
		log_warn("failed to check mode and encryption for parquet %s, skip", filename);
		return ret_vec;
	}

	if (is_compat_mode == false && is_encrypted == true) {
		log_info("parquet mode [v1] [encrypted]: %s", filename);
		if (false == parquet_resolve_and_set_decryption_properties(
						reader_properties, conf, filename)) {
			log_error("Can't read encrypted parquet due to no encryption config");
			return ret_vec;
		}
	} else if (is_compat_mode == false && is_encrypted == false) {
		log_info("parquet mode [v1]: %s", filename);
	} else {
		log_info("parquet mode [compat]: %s", filename);
	}

	vector<int> index_vector(keys.size());

	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();

		int num_row_groups =
		    file_metadata
		        ->num_row_groups(); // Get the number of RowGroups

		for (int r = 0; r < num_row_groups; ++r) {

			shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			int64_t values_read = 0;
			int64_t rows_read   = 0;
			int16_t definition_level;
			shared_ptr<parquet::ColumnReader> column_reader;

			column_reader = row_group_reader->Column(0);
			parquet::Int64Reader *int64_reader =
			    static_cast<parquet::Int64Reader *>(
			        column_reader.get());

			index_vector = get_keys_indexes(int64_reader, keys);
			// Get the Column Reader for the ByteArray column
			column_reader = row_group_reader->Column(1);
			parquet::ByteArrayReader *ba_reader =
			    static_cast<parquet::ByteArrayReader *>(
			        column_reader.get());

			for (const auto &index : index_vector) {
				if (-1 == index) {
					ret_vec.push_back(NULL);
					continue;
				}

				if (ba_reader->HasNext()) {
					ba_reader->Skip(index - 1);
				}

				if (ba_reader->HasNext()) {
					parquet::ByteArray value;
					rows_read = ba_reader->ReadBatch(1,
					    &definition_level, nullptr, &value,
					    &values_read);
					if (1 == rows_read &&
					    1 == values_read) {
						parquet_data_packet *pack =
						    (parquet_data_packet *)
						        malloc(sizeof(
						            parquet_data_packet));
						pack->data =
						    (uint8_t *) malloc(
						        value.len *
						        sizeof(uint8_t));
						memcpy(pack->data, value.ptr,
						    value.len);
						pack->size = value.len;
						ret_vec.push_back(pack);
					}
				}
			}
		}

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return ret_vec;
}

string extract_topic(const string &file_path) {
	// New format: <prefix>_<topic>-<start>~<end>_<index>_<md5>.parquet
	std::regex new_fmt(
	    R"(.*?/[^/]*_(.*)-\d+~\d+_(?:\d+_)?[a-fA-F0-9]{32}\.parquet)");
	std::smatch matches;
	if (std::regex_match(file_path, matches, new_fmt) &&
	    matches.size() > 1) {
		return matches[1];
	}

	// Legacy format fallback.
	std::regex old_fmt(
	    R"(.*?/[^/]*_(.*?)_[a-fA-F0-9]{32}-\d+~\d+\.parquet)");
	if (std::regex_match(file_path, matches, old_fmt) &&
	    matches.size() > 1) {
		return matches[1];
	}

	return "";
}

struct parquet_runtime_metadata {
	string topic;
	string key_id;
	string wrapped_key;
	string wrap_alg;
	string cipher;
};

static bool
parquet_get_runtime_metadata(
    conf_parquet *conf, const char *filename, parquet_runtime_metadata *out)
{
	if (filename == NULL || out == NULL) {
		return false;
	}
	(void) conf;

	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();
		auto kv = file_metadata->key_value_metadata();
		if (kv == nullptr) {
			return false;
		}

		int idx = kv->FindKey("nmq.topic");
		if (idx >= 0) {
			out->topic = kv->value(idx);
		}
		idx = kv->FindKey("nmq.key.id");
		if (idx >= 0) {
			out->key_id = kv->value(idx);
		}
		idx = kv->FindKey("nmq.key.wrapped");
		if (idx >= 0) {
			out->wrapped_key = kv->value(idx);
		}
		idx = kv->FindKey("nmq.key.wrap_alg");
		if (idx >= 0) {
			out->wrap_alg = kv->value(idx);
		}
		idx = kv->FindKey("nmq.enc.cipher");
		if (idx >= 0) {
			out->cipher = kv->value(idx);
		}
		return true;
	} catch (const exception &e) {
		log_warn("read parquet metadata failed: %s", e.what());
		return false;
	}
}

static string
parquet_topic_from_metadata(conf_parquet *conf, const char *filename)
{
	parquet_runtime_metadata md;
	if (parquet_get_runtime_metadata(conf, filename, &md)) {
		return md.topic;
	}
	return "";
}

static string
resolve_topic_with_fallback(conf_parquet *conf, const char *filename)
{
	string topic = parquet_topic_from_metadata(conf, filename);
	if (!topic.empty()) {
		return topic;
	}
	topic = extract_topic(filename == NULL ? "" : filename);
	if (!topic.empty()) {
		return topic;
	}
	return file_manager.find_topic_by_filename(filename);
}

static bool
load_key_from_metadata(
    conf_parquet *conf, const parquet_runtime_metadata &md, string *decoded_key)
{
	if (conf == NULL || decoded_key == NULL) {
		return false;
	}

	if (md.wrap_alg.empty() || md.wrapped_key.empty()) {
		return false;
	}

	char *plain_key = NULL;
	if (!conf_parquet_unwrap_runtime_key(
			md.wrapped_key.c_str(), md.wrap_alg.c_str(), &plain_key)) {
		return false;
	}

	if (plain_key == NULL || strlen(plain_key) == 0) {
		conf_parquet_free_runtime_key(plain_key);
		return false;
	}

	decoded_key->assign(plain_key);
	conf_parquet_free_runtime_key(plain_key);
	if (!decoded_key->empty()) {
		return true;
	}
	return false;
}

static bool
parquet_resolve_selected_decryption_key(conf_parquet *conf,
    const parquet_runtime_metadata *md, const char *filename,
    string *selected_key)
{
	if (conf == NULL || selected_key == NULL) {
		return false;
	}

	bool   has_local_key = (conf->encryption.key != NULL &&
	      strlen(conf->encryption.key) > 0);
	string local_key;
	if (has_local_key) {
		local_key = conf->encryption.key;
	}

	string metadata_key;
	bool   has_metadata_key = false;
	if (md != NULL) {
		has_metadata_key = load_key_from_metadata(conf, *md, &metadata_key);
		if (!has_metadata_key && !md->wrapped_key.empty()) {
			log_warn("Failed to unwrap metadata key for parquet %s",
			    filename == NULL ? "(null)" : filename);
		}
	}

	if (!has_local_key && !has_metadata_key) {
		log_error("No usable decryption key for parquet %s",
		    filename == NULL ? "(null)" : filename);
		return false;
	}

	if (has_metadata_key) {
		if (has_local_key && local_key != metadata_key) {
			log_warn(
			    "Parquet key mismatch for %s, prefer metadata key",
			    filename == NULL ? "(null)" : filename);
		}
		*selected_key = metadata_key;
		return true;
	}

	*selected_key = local_key;
	return true;
}

static bool
parquet_resolve_and_set_decryption_properties(
    parquet::ReaderProperties &reader_properties, conf_parquet *conf,
    const char *filename)
{
	parquet_runtime_metadata md;
	parquet_runtime_metadata *md_ptr = NULL;
	if (parquet_get_runtime_metadata(conf, filename, &md)) {
		md_ptr = &md;
	}

	string selected_key;
	if (!parquet_resolve_selected_decryption_key(
			conf, md_ptr, filename, &selected_key)) {
		return false;
	}

	return parquet_read_set_property(reader_properties, selected_key.c_str());
}

vector<parquet_data_packet *>
parquet_find_data_packet(
    conf_parquet *conf, char *filename, vector<uint64_t> keys)
{
	vector<parquet_data_packet *> ret_vec;
	string topic = resolve_topic_with_fallback(conf, filename);
	if (conf == NULL && !topic.empty()) {
		conf = file_manager.fetch_conf(topic);
	}
	if (conf == NULL) {
		ret_vec.resize(keys.size(), nullptr);
		log_error("cannot resolve conf for parquet file %s",
		    filename == NULL ? "(null)" : filename);
		return ret_vec;
	}

	parquet_runtime_metadata md;
	parquet_get_runtime_metadata(conf, filename, &md);
	if (topic.empty() && !md.topic.empty()) {
		topic = md.topic;
	}
	if (topic.empty() && conf->name != NULL) {
		topic = conf->name;
	}
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic.c_str());
		return ret_vec;
	}


	WAIT_FOR_AVAILABLE
	void *elem = NULL;
	auto queue = file_manager.fetch_queue(topic);
	pthread_mutex_lock(&parquet_queue_mutex);
	if (queue == NULL) {
		pthread_mutex_unlock(&parquet_queue_mutex);
		ret_vec.resize(keys.size(), nullptr);
		return ret_vec;
	}
	FOREACH_QUEUE(*queue, elem)
	{
		if (elem && nng_strcasecmp((char *) elem, filename) == 0) {
			goto find;
		}
	}

find:
	pthread_mutex_unlock(&parquet_queue_mutex);

	if (elem) {
		ret_vec = parquet_read(conf, (char *) elem, keys);
	} else {

		ret_vec.resize(keys.size(), nullptr);
		log_debug("Not find file %s in file queue", (char *) elem);
	}
	return ret_vec;
}

parquet_data_packet *
parquet_find_data_packet(conf_parquet *conf, char *filename, uint64_t key)
{
	string topic = resolve_topic_with_fallback(conf, filename);
	if (conf == NULL && !topic.empty()) {
		conf = file_manager.fetch_conf(topic);
	}
	if (conf == NULL) {
		log_error("cannot resolve conf for parquet file %s",
		    filename == NULL ? "(null)" : filename);
		return NULL;
	}

	parquet_runtime_metadata md;
	parquet_get_runtime_metadata(conf, filename, &md);
	if (topic.empty() && !md.topic.empty()) {
		topic = md.topic;
	}
	if (topic.empty() && conf->name != NULL) {
		topic = conf->name;
	}
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic.c_str());
		return NULL;
	}
	WAIT_FOR_AVAILABLE
	void *elem  = NULL;
	auto  queue = file_manager.fetch_queue(topic);

	pthread_mutex_lock(&parquet_queue_mutex);
	if (queue == NULL) {
		pthread_mutex_unlock(&parquet_queue_mutex);
		return NULL;
	}
	FOREACH_QUEUE(*queue, elem)
	{
		if (elem && nng_strcasecmp((char *) elem, filename) == 0) {
			goto find;
		}
	}

find:
	pthread_mutex_unlock(&parquet_queue_mutex);

	if (elem) {
		uint32_t size = 0;
		uint8_t *data = parquet_read(conf, (char *) elem, key, &size);
		if (size) {
			parquet_data_packet *pack =
			    (parquet_data_packet *) malloc(
			        sizeof(parquet_data_packet));
			pack->data = data;
			pack->size = size;
			return pack;
		} else {
			log_debug(
			    "No key %ld in file: %s", key, (char *) elem);
		}
	}
	log_debug("Not find file %s in file queue", (char *) elem);
	return NULL;
}

parquet_data_packet **
parquet_find_data_packets(
    conf_parquet *conf, char **filenames, uint64_t *keys, uint32_t len)
{
	unordered_map<char *, vector<uint64_t>> file_name_map;
	vector<parquet_data_packet *>           ret_vec;
	parquet_data_packet                   **packets = NULL;
	// Get the file map
	for (uint32_t i = 0; i < len; i++) {
		vector<uint64_t> key_vec;
		if (filenames[i] == NULL) {
			log_error("filenames[%d] is NULL, len is %d", i, len);
			return NULL;
		}
		if (auto s = file_name_map.find(filenames[i]);
		    s != file_name_map.end()) {
			s->second.push_back(keys[i]);
		} else {
			key_vec.push_back(keys[i]);
			file_name_map.insert(pair(filenames[i], key_vec));
		}
	}

	// Traverse the map and get the vector of parquet_data_packet
	for (const auto &entry : file_name_map) {
		char                   *filename = entry.first;
		const vector<uint64_t> &sizes    = entry.second;

		auto tmp = parquet_find_data_packet(conf, filename, sizes);
		ret_vec.insert(ret_vec.end(), tmp.begin(), tmp.end());
	}

	if (!ret_vec.empty()) {
		packets = (parquet_data_packet **) malloc(
		    sizeof(parquet_data_packet *) * len);
		copy(ret_vec.begin(), ret_vec.end(), packets);
	}

	return packets;
}

static vector<SchemaColumn>
get_filtered_schema(shared_ptr<parquet::RowGroupReader> row_group_reader,
    shared_ptr<parquet::FileMetaData> file_metadata, const char **schema,
    uint16_t schema_len)
{
	vector<SchemaColumn> schema_vec;
	int                  num_columns = file_metadata->num_columns();

	for (int i = 1; i < num_columns; i++) {
		const char *column_name =
		    file_metadata->schema()->Column(i)->name().c_str();
		if (schema_len > 0) {
			bool in_schema = false;
			for (int j = 0; j < schema_len; j++) {
				if (strcmp(column_name, schema[j]) == 0) {
					in_schema = true;
					break;
				}
			}
			if (!in_schema)
				continue;
		}
		SchemaColumn col;
		col.name   = strdup(column_name);
		col.reader = row_group_reader->Column(i);
		schema_vec.push_back(col);
	}
	return schema_vec;
}

static parquet_data_packet **
read_column_data(shared_ptr<parquet::ColumnReader> column_reader,
    const vector<int> &index_vector, int64_t batch_size,
    int &total_values_read)
{
    auto ba_reader =
        dynamic_pointer_cast<parquet::ByteArrayReader>(column_reader);
    if (!ba_reader->HasNext()) {
        log_error("Next is NULL");
        return nullptr;
    }

    ba_reader->Skip(index_vector[0]);
    vector<parquet_data_packet *> ret_vec;
    while (total_values_read < batch_size) {
        vector<int16_t>            def_levels(batch_size);
        vector<int16_t>            rep_levels(batch_size);
        vector<parquet::ByteArray> values(batch_size);
        int64_t                    values_read = 0;
        int64_t                    rows_read =
            ba_reader->ReadBatch(batch_size, def_levels.data(),
                rep_levels.data(), values.data(), &values_read);

        // No more rows; avoid infinite loop
        if (rows_read == 0) {
            break;
        }

        for (int64_t r = 0, i = 0; r < rows_read; r++) {

            if (def_levels[r] == 0) { // NULL value
                log_trace("Row %lld is NULL", r);
                ret_vec.push_back(nullptr);
            } else {
                parquet_data_packet *pack =
                    (parquet_data_packet *) malloc(
                        sizeof(parquet_data_packet));
                if (!pack) {
                    log_error("Memory allocation failed for parquet_data_packet");
                    for (auto p : ret_vec) {
                        if (p) {
                            free(p->data);
                            free(p);
                        }
                    }
                    return nullptr;
                }
                pack->data = (uint8_t *) malloc(values[i].len * sizeof(uint8_t));
                memcpy(pack->data, values[i].ptr, values[i].len);
                pack->size = values[i++].len;
                ret_vec.push_back(pack);
            }

            total_values_read += 1; // advance for every row, including NULLs

            if (batch_size == total_values_read) {
                parquet_data_packet **payload_arr = nullptr;
                if (!ret_vec.empty()) {
                    payload_arr =
                        (parquet_data_packet **) malloc(
                            sizeof(parquet_data_packet *) *
                            ret_vec.size());
                    copy(ret_vec.begin(), ret_vec.end(),
                        payload_arr);
                }
                return payload_arr;
            }
        }
    }

    // Early exit without fulfilling batch_size: free partial buffers and return nullptr
    for (auto p : ret_vec) {
        if (p) {
            free(p->data);
            free(p);
        }
    }

    return nullptr;
}

static vector<SchemaColumn>
get_all_schema_except_ts(shared_ptr<parquet::RowGroupReader> row_group_reader,
    shared_ptr<parquet::FileMetaData> file_metadata)
{
	vector<SchemaColumn> schema_vec;
	int num_columns = file_metadata->num_columns();
	for (int i = 1; i < num_columns; i++) {
		SchemaColumn col;
		col.name = strdup(file_metadata->schema()->Column(i)->name().c_str());
		col.reader = row_group_reader->Column(i);
		schema_vec.push_back(col);
	}
	return schema_vec;
}

static parquet_data_ret *parquet_read_payload(shared_ptr<parquet::RowGroupReader> row_group_reader, 
                                              shared_ptr<parquet::FileMetaData> file_metadata, 
                                              const char **schema, 
                                              uint16_t schema_len, 
                                              vector<int> &index_vector) {
    parquet_data_ret *ret = nullptr;
    vector<SchemaColumn> schema_vec;
    if (schema_len > 0) {
        schema_vec = get_filtered_schema(row_group_reader, file_metadata, schema, schema_len);
    } else {
        //all schema, do not include ts column
        log_warn("schema len <= 0 get all schema don't include ts column");
        schema_vec = get_all_schema_except_ts(row_group_reader, file_metadata);
    }
    vector<parquet_data_packet **> ret_rows_vec;
    int64_t batch_size = index_vector[1] - index_vector[0] + 1;

    for (const auto &col : schema_vec) {
        int total_values_read = 0;
        parquet_data_packet **payload_arr = read_column_data(col.reader, index_vector, batch_size, total_values_read);
        if (payload_arr) {
            ret_rows_vec.push_back(payload_arr);
        } else {
            ret_rows_vec.push_back(nullptr);
        }
    }

    if (!schema_vec.empty()) {
        ret = (parquet_data_ret *)malloc(sizeof(parquet_data_ret));
        if (!ret) {
            log_error("malloc failed");
            return ret;
        }
        memset(ret, 0, sizeof(*ret));
        ret->col_len = schema_vec.size();
        ret->payload_arr = (parquet_data_packet ***)malloc(sizeof(parquet_data_packet **) * ret->col_len);
        ret->schema = (char **)malloc(sizeof(char *) * ret->col_len);
        for (size_t i = 0; i < schema_vec.size(); ++i) {
            ret->schema[i] = schema_vec[i].name;
        }
        ret->row_len = batch_size;
        copy(ret_rows_vec.begin(), ret_rows_vec.end(), ret->payload_arr);
    }

    return ret;
}

static parquet_data_ret *
parquet_read_span_by_column(conf_parquet *conf, const char *filename, uint64_t keys[2],
    const char **schema, uint16_t schema_len)
{
	parquet_data_ret         *ret  = NULL;
	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	string exception_msg = "";
	bool is_compat_mode = false;
	bool is_encrypted = false;

	if (0 != parquet_check_is_compat_and_decrypt(
				(char *)filename, is_compat_mode, is_encrypted)) {
		log_warn("failed to check mode and encryption for parquet %s, skip", filename);
		return NULL;
	}

	if (is_compat_mode == false && is_encrypted == true) {
		log_info("parquet mode [v1] [encrypted]: %s", filename);
		if (false == parquet_resolve_and_set_decryption_properties(
						reader_properties, conf, filename)) {
			log_error("Can't read encrypted parquet due to no encryption config");
			return NULL;
		}
	} else if (is_compat_mode == false && is_encrypted == false) {
		log_info("parquet mode [v1]: %s", filename);
	} else {
		log_info("parquet mode [compat]: %s", filename);
	}

	vector<int> index_vector(2);

	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();

		if (parquet_file_schema_is_nested(file_metadata->schema())) {
			parquet_data *batch = NULL;
			if (parquet_read_file(conf, filename, &batch) != 0 ||
			    batch == NULL) {
				return NULL;
			}
			ret = (parquet_data_ret *) malloc(
			    sizeof(parquet_data_ret));
			if (ret == NULL) {
				parquet_data_free(batch);
				return NULL;
			}
			memset(ret, 0, sizeof(*ret));
			ret->row_len     = batch->row_len;
			ret->ts          = batch->ts;
			ret->schema_tree = batch->schema_tree;
			ret->root        = batch->root;
			batch->ts          = NULL;
			batch->schema_tree = NULL;
			batch->root        = NULL;
			parquet_data_free(batch);
			(void) schema;
			(void) schema_len;
			return ret;
		}

		int num_row_groups =
		    file_metadata
		        ->num_row_groups(); // Get the number of RowGroups

		for (int r = 0; r < num_row_groups; ++r) {

			vector<char *>                 schema_vec;
			vector<parquet_data_packet **> ret_rows_vec;
			vector<uint64_t>                ts;

			shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			shared_ptr<parquet::ColumnReader> column_reader;

			column_reader = row_group_reader->Column(0);

			parquet::Int64Reader *int64_reader =
			    static_cast<parquet::Int64Reader *>(
			        column_reader.get());

			index_vector = get_keys_indexes_fuzing(
			    int64_reader, ts, keys[0], keys[1]);
			if (-1 == index_vector[0] || -1 == index_vector[1]) {
				log_debug("row group %d: no keys in range", r);
				continue;
			}

			log_debug("start index: %lu, end index: %lu",
			    index_vector[0], index_vector[1]);
			ret = parquet_read_payload(row_group_reader,
			    file_metadata, schema, schema_len, index_vector);
			if (ret == NULL) {
				continue;
			}
			ret->ts =
			    (uint64_t *) malloc(sizeof(uint64_t) * ts.size());
			copy(ts.begin(), ts.end(), ret->ts);
		}

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return ret;
}

typedef enum { START_KEY, END_KEY } key_type;

static uint64_t
get_key(const char *filename, key_type type)
{
	uint64_t range[2] = { 0 };
	uint64_t res      = 0;
	get_range(filename, range);
	switch (type) {
	case START_KEY:
		res = range[0];
		break;
	case END_KEY:
		res = range[1];
		break;
	default:
		break;
	}
	return res;
}

parquet_filename_range **
parquet_get_file_ranges(uint64_t start_key, uint64_t end_key, char *topic)
{
	uint32_t len = 0;
	// Find filenames
	log_info("topic: %s, start_key: %lu, end_key: %lu", topic, start_key, end_key);
	conf_parquet *conf = file_manager.fetch_conf(topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic);
		return NULL;
	}

	const char **filenames = parquet_find_span(topic, start_key, end_key, &len);
	vector<parquet_filename_range *> range_vec;

	// Get all keys
	for (uint32_t i = 0; i < len; i++) {
		log_info("filename: %s", filenames[i]);

		parquet_filename_range *range =
		    (parquet_filename_range *) nng_alloc(
		        sizeof(parquet_filename_range));
		range->keys[0] = start_key;
		range->keys[1] = end_key;
		if (len > 1) {
			range->keys[0] = i == 0
			    ? start_key
			    : get_key(filenames[i], START_KEY);
			range->keys[1] = i == (len - 1)
			    ? end_key
			    : get_key(filenames[i], END_KEY);
		}

		range->filename = filenames[i];

		log_debug("file start_key: %lu, file end_key: %lu",
		    range->keys[0], range->keys[1]);

		range_vec.push_back(range);
	}

	if (!range_vec.empty()) {
		// Push NULL as terminate
		range_vec.push_back(NULL);
		parquet_filename_range **ranges =
		    (parquet_filename_range **) nng_alloc(
		        sizeof(parquet_filename_range) * range_vec.size());
		copy(range_vec.begin(), range_vec.end(), ranges);

		nng_free(filenames, len);
		return ranges;
	}

	nng_free(filenames, len);
	return NULL;
}

bool
parquet_get_key_span(
    const char **topicl, uint32_t sz, uint64_t **key_span, uint64_t **sums)
{
	*key_span = (uint64_t *) nng_alloc(sz * 2 * sizeof(uint64_t));
	*sums     = (uint64_t *) nng_alloc(sz * sizeof(uint64_t));
	if (!*key_span || !*sums) {
		if (*key_span)
			nng_free(*key_span, sz * 2 * sizeof(uint64_t));
		if (*sums)
			nng_free(*sums, sz * sizeof(uint64_t));
		log_error("malloc memory failed!");
		return false; // allocation failed
	}
	void *elem = NULL;
	memset(*key_span, 0, sz * 2 * sizeof(uint64_t));
	memset(*sums, 0, sz * sizeof(uint64_t));

	pthread_mutex_lock(&parquet_queue_mutex);

	for (int idx = 0; idx < (int) sz; ++idx) {
		char    *first_file       = NULL;
		char    *last_file        = NULL;
		uint64_t file_key_span[2] = { 0 };
		auto     queue = file_manager.fetch_queue(topicl[idx]);

		if (NULL != queue) {

			FOREACH_QUEUE(*queue, elem)
			{
				if (elem) {
					if (!first_file) // Only set at the
					                 // first time
						first_file = (char *) elem;
					last_file = (char *) elem;
				}
			}
			if (first_file) {
				get_range(first_file, (*key_span) + 2 * idx);
			}
			if (last_file) {
				get_range(last_file, file_key_span);
			}
			(*key_span)[2 * idx + 1] = file_key_span[1];
			(*sums)[idx] = file_manager.get_queue_sum(topicl[idx]);
		}
	}

	pthread_mutex_unlock(&parquet_queue_mutex);
	return true;
}

void
parquet_free_key_span(uint64_t *key_span, uint64_t *sums, uint32_t sz)
{
	if (key_span) {
		nng_free(key_span, sz * 2 * sizeof(uint64_t));
	}
	if (sums) {
		nng_free(sums, sz * sizeof(uint64_t));
	}
}

parquet_data_ret **
parquet_get_data_packets_in_range_by_column(parquet_filename_range *range,
    const char *topic, const char **schema, uint16_t schema_len,
    uint32_t *size)
{

	conf_parquet *conf = file_manager.fetch_conf(topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic);
		return NULL;
	}

	if (!range || !size) {
		log_error("range && size should not be NULL");
		return NULL;
	}

	parquet_data_ret **rets = NULL;

	if (range->filename) {
		// Search only one file
		parquet_data_ret *ret = parquet_read_span_by_column(conf, 
		    range->filename, range->keys, schema, schema_len);
		if (ret) {
			rets = (parquet_data_ret **) nng_alloc(
			    sizeof(parquet_data_ret *) * 1);
			log_debug("read span size: 1");
			*size = 1;
			*rets = ret;
		} else {
			return NULL;
		}

	} else {
		// Search multiple files
		vector<parquet_data_ret *> ret_vec;
		uint32_t                   len       = 0;
		uint64_t                   start_key = range->keys[0];
		uint64_t                   end_key   = range->keys[1];

		log_info("topic: %s, start_key: %lu, end_key: %lu", topic, start_key, end_key);
		const char **filenames =
		    parquet_find_span(topic, start_key, end_key, &len);

		for (uint32_t i = 0; i < len; i++) {
			log_debug("filename: %s", filenames[i]);

			uint64_t keys[2];
			keys[0] = start_key;
			keys[1] = end_key;
			if (len > 1) {
				keys[0] = i == 0
				    ? start_key
				    : get_key(filenames[i], START_KEY);
				keys[1] = i == (len - 1)
				    ? end_key
				    : get_key(filenames[i], END_KEY);
			}

			log_debug("file start_key: %lu, file end_key: %lu",
			    keys[0], keys[1]);

			auto ret = parquet_read_span_by_column(conf,
			    filenames[i], keys, schema, schema_len);

			ret_vec.push_back(ret);
			nng_strfree((char *) filenames[i]);
		}
		nng_free(filenames, len);

		if (!ret_vec.empty()) {
			rets = (parquet_data_ret **) nng_alloc(
			    sizeof(parquet_data_ret *) * ret_vec.size());
			copy(ret_vec.begin(), ret_vec.end(), rets);
			*size = ret_vec.size();
		}
	}

	return rets;
}
