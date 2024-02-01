#include <arrow/io/file.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>

#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/parquet.h"
#include "queue.h"
#include <assert.h>
#include <atomic>
#include <dirent.h>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

using namespace std;
using parquet::ConvertedType;
using parquet::Repetition;
using parquet::Type;
using parquet::schema::GroupNode;
using parquet::schema::PrimitiveNode;
#define PARQUET_END 1024

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

CircularQueue   parquet_queue;
CircularQueue   parquet_file_queue;
pthread_mutex_t parquet_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  parquet_queue_not_empty = PTHREAD_COND_INITIALIZER;
static conf_parquet *g_conf = NULL;

static bool
directory_exists(const std::string &directory_path)
{
	struct stat buffer;
	return (stat(directory_path.c_str(), &buffer) == 0 &&
	    S_ISDIR(buffer.st_mode));
}

static bool
create_directory(const std::string &directory_path)
{
	int status = mkdir(
	    directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	return (status == 0);
}

static char *
get_file_name(conf_parquet *conf, uint64_t key_start, uint64_t key_end)
{
	char *file_name = NULL;
	char *dir       = conf->dir;
	char *prefix    = conf->file_name_prefix;

	file_name = (char *) malloc(strlen(prefix) + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}

	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".parquet", dir,
	    prefix, key_start, key_end);
	ENQUEUE(parquet_file_queue, file_name);
	return file_name;
}

string
gen_random(const int len)
{
	static const char alphanum[] = "0123456789"
	                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                               "abcdefghijklmnopqrstuvwxyz";
	std::string       tmp_s;
	tmp_s.reserve(len);

	for (int i = 0; i < len; ++i) {
		tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
	}
	return tmp_s;
}

static char *
get_random_file_name(char *prefix, uint64_t key_start, uint64_t key_end)
{
	char *file_name = NULL;
	char dir[]       = "/tmp";

	file_name = (char *) malloc(strlen(prefix) + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}

	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".parquet", dir,
	    prefix, key_start, key_end);
	log_error("file_name: %s", file_name);
	return file_name;
}

static int
remove_old_file(void)
{
	char *filename = (char *) DEQUEUE(parquet_file_queue);
	if (remove(filename) == 0) {
		log_debug("File '%s' removed successfully.\n", filename);
	} else {
		log_error("Error removing the file %s", filename);
		return -1;
	}

	free(filename);
	return 0;
}

static shared_ptr<GroupNode>
setup_schema()
{
	parquet::schema::NodeVector fields;
	fields.push_back(parquet::schema::PrimitiveNode::Make("key",
	    parquet::Repetition::OPTIONAL, parquet::Type::INT64,
	    parquet::ConvertedType::UINT_64));
	fields.push_back(parquet::schema::PrimitiveNode::Make("data",
	    parquet::Repetition::OPTIONAL, parquet::Type::BYTE_ARRAY,
	    parquet::ConvertedType::UTF8));

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
		FREE_IF_NOT_NULL(
		    range->filename, strlen(range->filename));
		delete range;
	}
}

parquet_object *
parquet_object_alloc(uint64_t *keys, uint8_t **darray, uint32_t *dsize,
    uint32_t size, nng_aio *aio, void *arg)
{
	parquet_object *elem = new parquet_object;
	elem->keys           = keys;
	elem->darray         = darray;
	elem->dsize          = dsize;
	elem->size           = size;
	elem->aio            = aio;
	elem->arg            = arg;
	elem->ranges         = new parquet_file_ranges;
	elem->ranges->range  = NULL;
	elem->ranges->start  = 0;
	elem->ranges->size   = 0;
	return elem;
}

void
parquet_object_free(parquet_object *elem)
{
	if (elem) {
		FREE_IF_NOT_NULL(elem->keys, elem->size);
		FREE_IF_NOT_NULL(elem->dsize, elem->size);
		nng_aio_set_prov_data(elem->aio, elem->arg);
		nng_aio_set_output(elem->aio, 1, elem->ranges);
		uint32_t *szp = (uint32_t *) malloc(sizeof(uint32_t));
		*szp          = elem->size;
		nng_aio_set_msg(elem->aio, (nng_msg *) szp);
		DO_IT_IF_NOT_NULL(nng_aio_finish_sync, elem->aio, 0);
		FREE_IF_NOT_NULL(elem->darray, elem->size);
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
	elem->type = WRITE_TO_NORMAL;
	WAIT_FOR_AVAILABLE
	pthread_mutex_lock(&parquet_queue_mutex);
	if (IS_EMPTY(parquet_queue)) {
		pthread_cond_broadcast(&parquet_queue_not_empty);
	}
	ENQUEUE(parquet_queue, elem);
	log_debug("enqueue element.");

	pthread_mutex_unlock(&parquet_queue_mutex);

	return 0;
}

int  parquet_write_batch_tmp_async(parquet_object *elem)
{
	elem->type = WRITE_TO_TEMP;
	WAIT_FOR_AVAILABLE
	pthread_mutex_lock(&parquet_queue_mutex);
	if (IS_EMPTY(parquet_queue)) {
		pthread_cond_broadcast(&parquet_queue_not_empty);
	}
	ENQUEUE(parquet_queue, elem);
	log_debug("enqueue element.");

	pthread_mutex_unlock(&parquet_queue_mutex);

	return 0;
}


bool
need_new_one(const char *file_name, size_t file_max)
{
	struct stat st;
	if (stat(file_name, &st) != 0) {
		log_error("Failed to open the file.");
		return false;
	}
	// printf("File size: %d\n", st.st_size + PARQUET_END);
	// printf("File max: %d\n", file_max);
	return st.st_size >= (__off_t) (file_max - PARQUET_END);
}

shared_ptr<parquet::FileEncryptionProperties>
parquet_set_encryption(conf_parquet *conf)
{

	shared_ptr<parquet::FileEncryptionProperties>
	    encryption_configurations;

	// Encrypt all columns and the footer with
	// the same key. (uniform encryption)
	parquet::FileEncryptionProperties::Builder file_encryption_builder(
	    conf->encryption.key);
	encryption_configurations =
	    file_encryption_builder
	        .footer_key_metadata(conf->encryption.key_id)
	        ->algorithm(static_cast<parquet::ParquetCipher::type>(
	            conf->encryption.type))
	        ->build();

	return encryption_configurations;
}

static int
compute_new_index(parquet_object *obj, uint32_t index, uint32_t file_size)
{
	uint64_t size = 0;
	uint32_t new_index;
	for (new_index = index; size < file_size && new_index < obj->size - 1;
	     new_index++) {
		size += obj->dsize[new_index];
	}
	return new_index;
}

void
update_parquet_file_ranges(
    conf_parquet *conf, parquet_object *elem, parquet_file_range *range)
{
	if (elem->ranges->size != conf->file_count) {
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

int
parquet_write_tmp(
    conf_parquet *conf, shared_ptr<GroupNode> schema, parquet_object *elem)
{
	uint32_t old_index = 0;
	uint32_t new_index = 0;
again:

	new_index = compute_new_index(elem, old_index, conf->file_size);
	uint64_t key_start = elem->keys[old_index];
	uint64_t key_end   = elem->keys[new_index];

	string prefix = gen_random(6);
	prefix = "nanomq" + prefix;
	char *filename = get_random_file_name(prefix.data(), key_start, key_end);
	if (filename == NULL) {
		log_error("Failed to get file name");
		return -1;
	}

	{
		parquet_file_range *range = parquet_file_range_alloc(old_index, new_index, filename);
		update_parquet_file_ranges(conf, elem, range);

		// Create a ParquetFileWriter instance
		parquet::WriterProperties::Builder builder;

		builder.created_by("NanoMQ")
		    ->version(parquet::ParquetVersion::PARQUET_2_6)
		    ->data_page_version(parquet::ParquetDataPageVersion::V2)
		    ->compression(static_cast<arrow::Compression::type>(
		        conf->comp_type));

		if (conf->encryption.enable) {
			shared_ptr<parquet::FileEncryptionProperties>
			    encryption_configurations;
			encryption_configurations =
			    parquet_set_encryption(conf);
			builder.encryption(encryption_configurations);
		}

		shared_ptr<parquet::WriterProperties> props = builder.build();
		using FileClass = arrow::io::FileOutputStream;
		shared_ptr<FileClass> out_file;
		PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(filename));
		std::shared_ptr<parquet::ParquetFileWriter> file_writer =
		    parquet::ParquetFileWriter::Open(out_file, schema, props);

		// Append a RowGroup with a specific number of rows.
		parquet::RowGroupWriter *rg_writer =
		    file_writer->AppendRowGroup();

		// Write the Int64 column
		parquet::Int64Writer *int64_writer =
		    static_cast<parquet::Int64Writer *>(
		        rg_writer->NextColumn());
		for (uint32_t i = old_index; i <= new_index; i++) {
			int64_t value            = elem->keys[i];
			int16_t definition_level = 1;
			int64_writer->WriteBatch(
			    1, &definition_level, nullptr, &value);
		}

		// Write the ByteArray column. Make every alternate values NULL
		parquet::ByteArrayWriter *ba_writer =
		    static_cast<parquet::ByteArrayWriter *>(
		        rg_writer->NextColumn());
		for (uint32_t i = old_index; i <= new_index; i++) {
			parquet::ByteArray value;
			int16_t            definition_level = 1;
			value.ptr                           = elem->darray[i];
			value.len                           = elem->dsize[i];
			ba_writer->WriteBatch(
			    1, &definition_level, nullptr, &value);
		}

		old_index = new_index;

		FREE_IF_NOT_NULL(
		    filename, strlen(filename));

		if (new_index != elem->size - 1)
			goto again;
	}

	parquet_object_free(elem);
	return 0;
}

int
parquet_write(
    conf_parquet *conf, shared_ptr<GroupNode> schema, parquet_object *elem)
{
	uint32_t old_index = 0;
	uint32_t new_index = 0;
again:

	new_index = compute_new_index(elem, old_index, conf->file_size);
	uint64_t key_start = elem->keys[old_index];
	uint64_t key_end   = elem->keys[new_index];
	pthread_mutex_lock(&parquet_queue_mutex);
	char *filename = get_file_name(conf, key_start, key_end);
	if (filename == NULL) {
		pthread_mutex_unlock(&parquet_queue_mutex);
		log_error("Failed to get file name");
		return -1;
	}

	if (QUEUE_SIZE(parquet_file_queue) > conf->file_count) {
		remove_old_file();
	}
	pthread_mutex_unlock(&parquet_queue_mutex);

	{
		parquet_file_range *range = parquet_file_range_alloc(old_index, new_index, filename);
		update_parquet_file_ranges(conf, elem, range);

		// Create a ParquetFileWriter instance
		parquet::WriterProperties::Builder builder;

		builder.created_by("NanoMQ")
		    ->version(parquet::ParquetVersion::PARQUET_2_6)
		    ->data_page_version(parquet::ParquetDataPageVersion::V2)
		    ->compression(static_cast<arrow::Compression::type>(
		        conf->comp_type));

		if (conf->encryption.enable) {
			shared_ptr<parquet::FileEncryptionProperties>
			    encryption_configurations;
			encryption_configurations =
			    parquet_set_encryption(conf);
			builder.encryption(encryption_configurations);
		}

		shared_ptr<parquet::WriterProperties> props = builder.build();
		using FileClass = arrow::io::FileOutputStream;
		shared_ptr<FileClass> out_file;
		PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(filename));
		std::shared_ptr<parquet::ParquetFileWriter> file_writer =
		    parquet::ParquetFileWriter::Open(out_file, schema, props);

		// Append a RowGroup with a specific number of rows.
		parquet::RowGroupWriter *rg_writer =
		    file_writer->AppendRowGroup();

		// Write the Int64 column
		parquet::Int64Writer *int64_writer =
		    static_cast<parquet::Int64Writer *>(
		        rg_writer->NextColumn());
		for (uint32_t i = old_index; i <= new_index; i++) {
			int64_t value            = elem->keys[i];
			int16_t definition_level = 1;
			int64_writer->WriteBatch(
			    1, &definition_level, nullptr, &value);
		}

		// Write the ByteArray column. Make every alternate values NULL
		parquet::ByteArrayWriter *ba_writer =
		    static_cast<parquet::ByteArrayWriter *>(
		        rg_writer->NextColumn());
		for (uint32_t i = old_index; i <= new_index; i++) {
			parquet::ByteArray value;
			int16_t            definition_level = 1;
			value.ptr                           = elem->darray[i];
			value.len                           = elem->dsize[i];
			ba_writer->WriteBatch(
			    1, &definition_level, nullptr, &value);
		}

		old_index = new_index;

		if (new_index != elem->size - 1)
			goto again;
	}

	parquet_object_free(elem);
	return 0;
}

void
parquet_write_loop_v2(void *config)
{
	if (config == NULL) {
		log_error("parquet conf is NULL");
	}

	conf_parquet *conf = (conf_parquet *) config;
	if (!directory_exists(conf->dir)) {
		if (!create_directory(conf->dir)) {
			log_error("Failed to create directory %s", conf->dir);
			return;
		}
	}

	shared_ptr<GroupNode> schema = setup_schema();

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
		case WRITE_TO_NORMAL:
			parquet_write(conf, schema, ele);
			break;
		case WRITE_TO_TEMP:
			parquet_write_tmp(conf, schema, ele);
			break;
		default:
			break;
		}

	}
}

int
parquet_write_launcher(conf_parquet *conf)
{
	// Using a global variable g_conf temporarily, because it is inconvenient to access conf in exchange.
	g_conf = conf;
	INIT_QUEUE(parquet_queue);
	INIT_QUEUE(parquet_file_queue);
	is_available = true;
	thread write_loop(parquet_write_loop_v2, conf);
	write_loop.detach();
	return 0;
}

static void
get_range(const char *name, uint64_t range[2])
{
	const char *start = strrchr(name, '-');
	sscanf(start, "-%ld~%ld.parquet", &range[0], &range[1]);
	return;
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
parquet_find(uint64_t key)
{
	WAIT_FOR_AVAILABLE
	const char *value = NULL;
	void       *elem  = NULL;
	pthread_mutex_lock(&parquet_queue_mutex);
	FOREACH_QUEUE(parquet_file_queue, elem)
	{
		if (elem && compare_callback(elem, key)) {
			value = nng_strdup((char *) elem);
			break;
		}
	}
	pthread_mutex_unlock(&parquet_queue_mutex);
	return value;
}

const char **
parquet_find_span(uint64_t start_key, uint64_t end_key, uint32_t *size)
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
	if (parquet_file_queue.size != 0) {
		array = (const char **) nng_alloc(
		    sizeof(char *) * parquet_file_queue.size);

		ret = array;
		FOREACH_QUEUE(parquet_file_queue, elem)
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

static uint8_t *
parquet_read(conf_parquet *conf, char *filename, uint64_t key, uint32_t *len)
{
	conf = g_conf;
	std::string path_int64 = "key";
	std::string path_str   = "data";
	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	if (conf->encryption.enable) {
		std::map<std::string,
		    std::shared_ptr<parquet::ColumnDecryptionProperties>>
		                                             decryption_cols;
		parquet::ColumnDecryptionProperties::Builder decryption_col_builder31(
		    path_int64);
		parquet::ColumnDecryptionProperties::Builder decryption_col_builder32(
		    path_str);

		parquet::FileDecryptionProperties::Builder file_decryption_builder_3;
		std::shared_ptr<parquet::FileDecryptionProperties>
		    decryption_configuration =
		        file_decryption_builder_3.footer_key(conf->encryption.key)
		            ->column_keys(decryption_cols)
		            ->build();

		// Add the current decryption configuration to ReaderProperties.
		reader_properties.file_decryption_properties(
		    decryption_configuration->DeepClone());

	}

	// Create a ParquetReader instance
	std::string exception_msg = "";
	try {
		std::unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		std::shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();

		int num_row_groups =
		    file_metadata
		        ->num_row_groups(); // Get the number of RowGroups
		int num_columns =
		    file_metadata->num_columns(); // Get the number of Columns
		assert(num_columns == 2);

		for (int r = 0; r < num_row_groups; ++r) {

			std::shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			string  strCont;
			int64_t values_read = 0;
			int64_t rows_read   = 0;
			int16_t definition_level;
			int16_t repetition_level;
			std::shared_ptr<parquet::ColumnReader> column_reader;

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
					if (value == key)
						break;
				}
				i++;
			}

			// Get the Column Reader for the ByteArray column
			column_reader = row_group_reader->Column(1);
			parquet::ByteArrayReader *ba_reader =
			    static_cast<parquet::ByteArrayReader *>(
			        column_reader.get());

			int j = 0;
			while (ba_reader->HasNext()) {
				parquet::ByteArray value;
				rows_read =
				    ba_reader->ReadBatch(1, &definition_level,
				        nullptr, &value, &values_read);
				if (1 == rows_read && 1 == values_read) {
					string strTemp = string(
					    (char *) value.ptr, value.len);
					if (j == i) {
						uint8_t *ret =
						    (uint8_t *) malloc(
						        value.len *
						        sizeof(uint8_t));
						memcpy(
						    ret, value.ptr, value.len);
						*len = value.len;
						return ret;
					}
				}
				j++;
			}
		}

	} catch (const std::exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return NULL;
}

parquet_data_packet *
parquet_find_data_packet(conf_parquet *conf, char *filename, uint64_t key)
{
	WAIT_FOR_AVAILABLE
	void *elem = NULL;
	pthread_mutex_lock(&parquet_queue_mutex);
	FOREACH_QUEUE(parquet_file_queue, elem)
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
	parquet_data_packet **packets = (parquet_data_packet **) malloc(
	    sizeof(parquet_data_packet *) * len);
	for (uint32_t i = 0; i < len; i++) {
		packets[i] =
		    parquet_find_data_packet(conf, filenames[i], keys[i]);
	}
	return packets;
}
