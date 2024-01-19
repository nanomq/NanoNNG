#include <arrow/io/file.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>

#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/parquet.h"
#include "queue.h"
#include <assert.h>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <atomic>
#include <inttypes.h>


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


#define _Atomic(X) std::atomic< X >
atomic_bool is_available = false;
#define WAIT_FOR_AVAILABLE	while (!is_available) nng_msleep(10);

#define UINT64_MAX_DIGITS 20

CircularQueue   parquet_queue;
CircularQueue   parquet_file_queue;
pthread_mutex_t parquet_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  parquet_queue_not_empty = PTHREAD_COND_INITIALIZER;

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
get_file_name(conf_parquet *conf)
{
	char   *dir    = conf->dir;
	char   *prefix = conf->file_name_prefix;
	uint8_t index  = conf->file_index++;
	if (index >= conf->file_count) {
		index            = 0;
		conf->file_index = 1;
	}

	char *file_name = (char *) malloc(strlen(prefix) + strlen(dir) + 16);
	sprintf(file_name, "%s/%s%d.parquet", dir, prefix, index);
	return file_name;
}

static char *
get_file_name_v2(conf_parquet *conf, parquet_object *object)
{
	uint64_t key_start = object->keys[0];
	uint64_t key_end   = object->keys[object->size - 1];
	char    *file_name = NULL;
	char    *dir       = conf->dir;
	char    *prefix    = conf->file_name_prefix;
	uint8_t  index     = conf->file_index++;
	if (index >= conf->file_count) {
		index            = 0;
		conf->file_index = 1;
	}

	file_name = (char *)malloc(strlen(prefix) + strlen(dir) + UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}

	sprintf(
	    file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".parquet", dir, prefix, key_start, key_end);
	ENQUEUE(parquet_file_queue, file_name);
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
	return elem;
}

void
parquet_object_free(parquet_object *elem)
{
	if (elem) {
		FREE_IF_NOT_NULL(elem->keys, elem->size);
		FREE_IF_NOT_NULL(elem->darray, elem->size);
		FREE_IF_NOT_NULL(elem->dsize, elem->size);
		nng_aio_set_prov_data(elem->aio, elem->arg);
		uint32_t *szp = (uint32_t *)malloc(sizeof(uint32_t));
		*szp = elem->size;
		nng_aio_set_msg(elem->aio, (nng_msg *)szp);
		DO_IT_IF_NOT_NULL(nng_aio_finish_sync, elem->aio, 0);
		delete elem;
	}
}

int
parquet_write_batch_async(parquet_object *elem)
{
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

int
parquet_write(std::shared_ptr<parquet::ParquetFileWriter> file_writer,
    parquet_object                                       *elem)
{

	// Append a RowGroup with a specific number of rows.
	parquet::RowGroupWriter *rg_writer = file_writer->AppendRowGroup();

	// Write the Int64 column
	parquet::Int64Writer *int64_writer =
	    static_cast<parquet::Int64Writer *>(rg_writer->NextColumn());
	for (uint32_t i = 0; i < elem->size; i++) {
		int64_t value            = elem->keys[i];
		int16_t definition_level = 1;
		int64_writer->WriteBatch(
		    1, &definition_level, nullptr, &value);
	}

	// Write the ByteArray column. Make every alternate values NULL
	parquet::ByteArrayWriter *ba_writer =
	    static_cast<parquet::ByteArrayWriter *>(rg_writer->NextColumn());
	for (uint32_t i = 0; i < elem->size; i++) {
		parquet::ByteArray value;
		int16_t            definition_level = 1;
		value.ptr                           = elem->darray[i];
		value.len                           = elem->dsize[i];
		ba_writer->WriteBatch(1, &definition_level, nullptr, &value);
	}

	parquet_object_free(elem);
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
	return st.st_size >= (__off_t)(file_max - PARQUET_END);
}

void
parquet_write_loop(void *config)
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

	char *filename = get_file_name(conf);
	if (filename == NULL) {
		log_error("Failed to get file name");
		return;
	}

	using FileClass = arrow::io::FileOutputStream;
	shared_ptr<FileClass> out_file;
	PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(filename));

	shared_ptr<parquet::WriterProperties> props =
	    parquet::WriterProperties::Builder()
	        .created_by("NanoMQ")
	        ->version(parquet::ParquetVersion::PARQUET_2_6)
	        ->data_page_version(parquet::ParquetDataPageVersion::V2)
	        ->compression(
	            static_cast<arrow::Compression::type>(conf->comp_type))
	        ->build();

	shared_ptr<GroupNode> schema = setup_schema();

	// Create a ParquetFileWriter instance
	std::shared_ptr<parquet::ParquetFileWriter> file_writer =
	    parquet::ParquetFileWriter::Open(out_file, schema, props);

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
		parquet_write(file_writer, ele);

		if (need_new_one(filename, conf->file_size)) {
			file_writer->Close();
			free(filename);
			filename = get_file_name(conf);
			if (filename == NULL) {
				log_error("Failed to get file name");
				return;
			}
			PARQUET_ASSIGN_OR_THROW(
			    out_file, FileClass::Open(filename));
			file_writer = parquet::ParquetFileWriter::Open(
			    out_file, schema, props);
		}
	}

	return;
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

	using FileClass = arrow::io::FileOutputStream;

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

		char *filename = get_file_name_v2(conf, ele);
		if (filename == NULL) {
			log_error("Failed to get file name");
			return;
		}

		if (QUEUE_SIZE(parquet_file_queue) > conf->file_count) {
			remove_old_file();
		}
		pthread_mutex_unlock(&parquet_queue_mutex);

		// Create a ParquetFileWriter instance
		parquet::WriterProperties::Builder builder;

		builder.created_by("NanoMQ")
		    ->version(parquet::ParquetVersion::PARQUET_2_6)
		    ->data_page_version(parquet::ParquetDataPageVersion::V2)
		    ->compression(
		        static_cast<arrow::Compression::type>(conf->comp_type));

		if (conf->encryption.enable) {
			shared_ptr<parquet::FileEncryptionProperties>
			    encryption_configurations;
			encryption_configurations = parquet_set_encryption(conf);
			builder.encryption(encryption_configurations);
		}
		shared_ptr<parquet::WriterProperties> props = builder.build();

		shared_ptr<FileClass> out_file;
		PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(filename));
		std::shared_ptr<parquet::ParquetFileWriter> file_writer =
		    parquet::ParquetFileWriter::Open(out_file, schema, props);

		parquet_write(file_writer, ele);
	}
}

int
parquet_write_launcher(conf_parquet *conf)
{
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
	get_range((const char *)name, range);
	return (key >= range[0] && key <= range[1]);
}

static bool
compare_callback_span(void *name, uint64_t low, uint64_t high)
{
	uint64_t range[2] = { 0 };
	get_range((const char *)name, range);
	return !(low > range[1] || high < range[0]);
}

const char *
parquet_find(uint64_t key)
{
	WAIT_FOR_AVAILABLE
	const char *value = NULL;
	void *elem = NULL;
	pthread_mutex_lock(&parquet_queue_mutex);
	FOREACH_QUEUE(parquet_file_queue, elem) {
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

	uint64_t low = start_key;
	uint64_t high = end_key;
	uint32_t local_size = 0;
	const char *value = NULL;
	const char **array = NULL;
	const char **ret = NULL;
	void *elem = NULL;

	pthread_mutex_lock(&parquet_queue_mutex);
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
	pthread_mutex_unlock(&parquet_queue_mutex);
	(*size) = local_size;
	return ret;
}
