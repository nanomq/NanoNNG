#include <arrow/io/file.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>

#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/md5.h"
#include "nng/supplemental/nanolib/parquet.h"
#include "nng/supplemental/nanolib/queue.h"
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

CircularQueue        parquet_queue;
CircularQueue        parquet_file_queue;
pthread_mutex_t      parquet_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t       parquet_queue_not_empty = PTHREAD_COND_INITIALIZER;
static conf_parquet *g_conf                  = NULL;

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
	char  dir[]     = "/tmp";

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
	int   ret      = 0;
	char *filename = (char *) DEQUEUE(parquet_file_queue);
	if (remove(filename) == 0) {
		log_debug("File '%s' removed successfully.\n", filename);
	} else {
		log_error(
		    "Error removing the file %s errno: %d", filename, errno);
		ret = -1;
	}

	free(filename);
	return ret;
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
		FREE_IF_NOT_NULL(range->filename, strlen(range->filename));
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
		log_debug("finish write aio");
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
	if (g_conf == NULL || g_conf->enable == false) {
		log_error("Parquet is not ready or not launch!");
		return -1;
	}
	elem->type = WRITE_TO_NORMAL;
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
	if (g_conf == NULL || g_conf->enable == false) {
		log_error("Parquet is not ready or not launch!");
		return -1;
	}
	elem->type = WRITE_TO_TEMP;
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
	prefix        = "nanomq" + prefix;
	char *filename =
	    get_random_file_name(prefix.data(), key_start, key_end);
	if (filename == NULL) {
		log_error("Failed to get file name");
		parquet_object_free(elem);
		return -1;
	}

	{
		parquet_file_range *range =
		    parquet_file_range_alloc(old_index, new_index, filename);
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

		FREE_IF_NOT_NULL(filename, strlen(filename));

		if (new_index != elem->size - 1)
			goto again;
	}

	parquet_object_free(elem);
	return 0;
}
char *
compute_and_rename_file_withMD5(char *filename, conf_parquet *conf, char *topic)
{
	char md5_buffer[MD5_LEN + 1];
	log_debug("compute md5");
	int ret = ComputeFileMD5(filename, md5_buffer);
	if (ret != 0) {
		log_error("Failed to calculate md5sum");
		ret = remove(filename);
		if (ret != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename, errno);
		}

		free(filename);
		return NULL;
	}

	char *md5_file_name = (char *) malloc(
	    strlen(filename) + strlen("_") + strlen(topic) + strlen("_") + strlen(md5_buffer) + 2);
	if (md5_file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		ret = remove(filename);
		if (ret != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename, errno);
		}

		free(filename);
		return NULL;
	}

	strncpy(md5_file_name, filename,
	    strlen(conf->dir) + strlen(conf->file_name_prefix) + 1);
	md5_file_name[strlen(conf->dir) + strlen(conf->file_name_prefix) + 1] =
	    '\0';

	strcat(md5_file_name, "_");
	strcat(md5_file_name, topic);
	strcat(md5_file_name, "_");
	strcat(md5_file_name, md5_buffer);
	strcat(md5_file_name,
	    filename + strlen(conf->dir) + strlen(conf->file_name_prefix) + 1);
	log_info("trying to rename... %s to %s", filename, md5_file_name);
	ret = rename(filename, md5_file_name);
	if (ret != 0) {
		log_error("Failed to rename file %s to %s errno: %d", filename,
		    md5_file_name, errno);
		ret = remove(filename);
		if (ret != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename, errno);
		}

		free(filename);
		free(md5_file_name);
		return NULL;
	}

	free(filename);
	return md5_file_name;
}

int
parquet_write(
    conf_parquet *conf, shared_ptr<GroupNode> schema, parquet_object *elem)
{
	uint32_t old_index      = 0;
	uint32_t new_index      = 0;
	char    *last_file_name = NULL;
again:

	if (last_file_name != NULL) {
		char *md5_file_name =
		    compute_and_rename_file_withMD5(last_file_name, conf, elem->topic);
		if (md5_file_name == nullptr) {
			log_error("Failed to rename file with md5");
			parquet_object_free(elem);
			return -1;
		}

		char md5_buffer[MD5_LEN + 1];
		log_debug("compute md5 after rename");
		int ret = ComputeFileMD5(md5_file_name, md5_buffer);
		if (ret != 0) {
			log_error("Failed to calculate md5sum");
		}
		log_debug("wait for parquet_queue_mutex");
		pthread_mutex_lock(&parquet_queue_mutex);
		ENQUEUE(parquet_file_queue, md5_file_name);

		if (QUEUE_SIZE(parquet_file_queue) > conf->file_count) {
			remove_old_file();
		}

		pthread_mutex_unlock(&parquet_queue_mutex);
		last_file_name = NULL;
	}
	log_debug("parquet_write");
	new_index = compute_new_index(elem, old_index, conf->file_size);
	uint64_t key_start = elem->keys[old_index];
	uint64_t key_end   = elem->keys[new_index];
	char    *filename  = get_file_name(conf, key_start, key_end);
	if (filename == NULL) {
		parquet_object_free(elem);
		log_error("Failed to get file name");
		return -1;
	}

	{
		parquet_file_range *range =
		    parquet_file_range_alloc(old_index, new_index, filename);
		update_parquet_file_ranges(conf, elem, range);

		// Create a ParquetFileWriter instance
		parquet::WriterProperties::Builder builder;
		log_debug("init builder");
		builder.created_by("NanoMQ")
		    ->version(parquet::ParquetVersion::PARQUET_2_6)
		    ->data_page_version(parquet::ParquetDataPageVersion::V2)
		    ->compression(static_cast<arrow::Compression::type>(
		        conf->comp_type));
		log_debug("check encry");
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
		log_debug("start doing int64 write");
		parquet::Int64Writer *int64_writer =
		    static_cast<parquet::Int64Writer *>(
		        rg_writer->NextColumn());
		for (uint32_t i = old_index; i <= new_index; i++) {
			int64_t value            = elem->keys[i];
			int16_t definition_level = 1;
			int64_writer->WriteBatch(
			    1, &definition_level, nullptr, &value);
		}
		log_debug("stop doing int64 write");

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
		log_debug("stop doing ByteArray write");

		old_index = new_index;

		last_file_name = filename;

		if (new_index != elem->size - 1)
			goto again;
	}
	if (last_file_name != NULL) {
		char *md5_file_name =
		    compute_and_rename_file_withMD5(last_file_name, conf, elem->topic);
		if (md5_file_name == nullptr) {
			parquet_object_free(elem);
			log_error("fail to get md5 from parquet file");
			return -1;
		}

		char md5_buffer[MD5_LEN + 1];
		log_debug("compute md5 after rename");
		int ret = ComputeFileMD5(md5_file_name, md5_buffer);
		if (ret != 0) {
			log_error("Failed to calculate md5sum");
		}
		log_debug("wait for parquet_queue_mutex");
		pthread_mutex_lock(&parquet_queue_mutex);
		ENQUEUE(parquet_file_queue, md5_file_name);

		if (QUEUE_SIZE(parquet_file_queue) > conf->file_count) {
			remove_old_file();
		}

		pthread_mutex_unlock(&parquet_queue_mutex);
		last_file_name = NULL;
	}

	log_info("flush finished!");
	parquet_object_free(elem);
	return 0;
}

void *
parquet_write_loop_v2(void *config)
{
	if (config == NULL) {
		log_error("parquet conf is NULL");
	}

	conf_parquet *conf = (conf_parquet *) config;
	if (!directory_exists(conf->dir)) {
		if (!create_directory(conf->dir)) {
			log_error("Failed to create directory %s", conf->dir);
			return NULL;
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
	return NULL;
}

static void
parquet_file_queue_init(conf_parquet *conf)
{
	DIR           *dir;
	struct dirent *ent;
	INIT_QUEUE(parquet_file_queue);

	if ((dir = opendir(conf->dir)) != NULL) {
		int count = 0;
		while ((ent = readdir(dir)) != NULL) {
			if (strstr(ent->d_name, ".parquet") != NULL) {
				char *file_path =
				    (char *) malloc(strlen(conf->dir) +
				        strlen(ent->d_name) + 2);
				if (file_path == NULL) {
					log_error("Failed to allocate memory "
					          "for file path.");
					closedir(dir);
					return;
				}
				sprintf(file_path, "%s/%s", conf->dir,
				    ent->d_name);

				if (strstr(ent->d_name, "_") == NULL) {
					if (unlink(file_path) != 0) {
						log_error("Failed to remove file %s errno: %d",
						    file_path, errno);
					} else {
						log_warn("Found a file without md5sum, "
					         "delete it: %s", file_path);
					}
					free(file_path);
					continue;
				}

				if (++count > conf->file_count) {
					if (0 == remove_old_file()) {
						log_info("To delete files "
						         "exceeding a certain "
						         "file count: %d.",
						    conf->file_count);
					} else {
						free(file_path);
						return;
					}
				}
				ENQUEUE(parquet_file_queue, file_path);
			}
		}
		int load_num =
		    count > conf->file_count ? conf->file_count : count;
		int remove_num =
		    count > conf->file_count ? count - conf->file_count : 0;
		log_info("Found %d parquet file from %s, load %d file, remove "
		         "%d file.",
		    count, conf->dir, load_num, remove_num);
		closedir(dir);
	} else {
		log_info("parquet directory not found, creating new one");
	}
}

int
parquet_write_launcher(conf_parquet *conf)
{
	// Using a global variable g_conf temporarily, because it is
	// inconvenient to access conf in exchange.
	g_conf = conf;
	INIT_QUEUE(parquet_queue);
	parquet_file_queue_init(conf);
	is_available = true;
	pthread_t write_thread;
	int result = 0;
	result = pthread_create(&write_thread, NULL, parquet_write_loop_v2, conf);
	if (result != 0) {
		log_error("Failed to create parquet write thread.");
		return -1;
	}

	return 0;
}

static void
get_range(const char *name, uint64_t range[2])
{
	//{prefix}_{md5}-{start_key}~{end_key}.parquet
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
	if (g_conf == NULL || g_conf->enable == false) {
		log_error("Parquet is not ready or not launch!");
		return NULL;
	}
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
	if (g_conf == NULL || g_conf->enable == false) {
		log_error("Parquet is not ready or not launch!");
		return NULL;
	}
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

void
parquet_read_set_property(
    parquet::ReaderProperties &reader_properties, conf_parquet *conf)
{
	if (conf->encryption.enable) {
		std::map<std::string,
		    std::shared_ptr<parquet::ColumnDecryptionProperties>>
		    decryption_cols;
		parquet::FileDecryptionProperties::Builder
		    file_decryption_builder_3;
		std::shared_ptr<parquet::FileDecryptionProperties>
		    decryption_configuration =
		        file_decryption_builder_3
		            .footer_key(conf->encryption.key)
		            ->column_keys(decryption_cols)
		            ->build();

		// Add the current decryption configuration to
		// ReaderProperties.
		reader_properties.file_decryption_properties(
		    decryption_configuration->DeepClone());
	}

	return;
}

static uint8_t *
parquet_read(conf_parquet *conf, char *filename, uint64_t key, uint32_t *len)
{
	conf                                 = g_conf;
	std::string               path_int64 = "key";
	std::string               path_str   = "data";
	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	parquet_read_set_property(reader_properties, conf);

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

	} catch (const std::exception &e) {
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

	int  index     = 0;
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
    parquet::Int64Reader *int64_reader, uint64_t start_key, uint64_t end_key)
{
	vector<int> index_vector;
	int64_t     values_read = 0;
	int64_t     rows_read   = 0;
	int16_t     definition_level;
	int16_t     repetition_level;

	int  index = 0;
	bool found = false;

	while (int64_reader->HasNext()) {
		int64_t value;
		rows_read = int64_reader->ReadBatch(1, &definition_level,
		    &repetition_level, &value, &values_read);
		if (1 == rows_read && 1 == values_read) {
			if (((uint64_t) value) >= start_key) {
				index_vector.push_back(index++);
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
			if (1 == rows_read && 1 == values_read) {
				if (((uint64_t) value) > end_key) {
					index_vector.push_back(index - 1);
					found = true;
					break;
				} else if (((uint64_t) value) == end_key) {
					index_vector.push_back(index);
					found = true;
					break;
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
	conf = g_conf;
	vector<parquet_data_packet *> ret_vec;
	std::string                   path_int64 = "key";
	std::string                   path_str   = "data";
	parquet::ReaderProperties     reader_properties =
	    parquet::default_reader_properties();

	parquet_read_set_property(reader_properties, conf);
	vector<int> index_vector(keys.size());

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
			int64_t values_read = 0;
			int64_t rows_read   = 0;
			int16_t definition_level;
			std::shared_ptr<parquet::ColumnReader> column_reader;

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

	} catch (const std::exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return ret_vec;
}

vector<parquet_data_packet *>
parquet_find_data_packet(
    conf_parquet *conf, char *filename, vector<uint64_t> keys)
{
	vector<parquet_data_packet *> ret_vec;
	if (g_conf == NULL || g_conf->enable == false) {
		log_error("Parquet is not ready or not launch!");
		return ret_vec;
	}
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
	if (g_conf == NULL || g_conf->enable == false) {
		log_error("Parquet is not ready or not launch!");
		return NULL;
	}
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
	unordered_map<char *, vector<uint64_t>> file_name_map;
	vector<parquet_data_packet *>           ret_vec;
	parquet_data_packet                   **packets = NULL;
	// Get the file map
	for (uint32_t i = 0; i < len; i++) {
		vector<uint64_t> key_vec;
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
		char		        *filename = entry.first;
		const std::vector<uint64_t> &sizes    = entry.second;

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

static vector<parquet_data_packet *>
parquet_read_span(conf_parquet *conf, const char *filename, uint64_t keys[2])
{
	conf = g_conf;
	vector<parquet_data_packet *> ret_vec;
	std::string                   path_int64 = "key";
	std::string                   path_str   = "data";
	parquet::ReaderProperties     reader_properties =
	    parquet::default_reader_properties();

	parquet_read_set_property(reader_properties, conf);
	vector<int> index_vector(2);

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
		assert(num_row_groups == 1);
		assert(num_columns == 2);

		for (int r = 0; r < num_row_groups; ++r) {

			std::shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			int64_t values_read = 0;
			int64_t rows_read   = 0;
			std::shared_ptr<parquet::ColumnReader> column_reader;

			column_reader = row_group_reader->Column(0);
			parquet::Int64Reader *int64_reader =
			    static_cast<parquet::Int64Reader *>(
			        column_reader.get());

			index_vector = get_keys_indexes_fuzing(
			    int64_reader, keys[0], keys[1]);
			if (-1 == index_vector[0] || -1 == index_vector[1]) {
				ret_vec.push_back(NULL);
				return ret_vec;
			}
			// Get the Column Reader for the ByteArray column
			column_reader = row_group_reader->Column(1);
			auto ba_reader =
			    dynamic_pointer_cast<parquet::ByteArrayReader>(
			        column_reader);

			if (ba_reader->HasNext()) {
				ba_reader->Skip(index_vector[0] - 1);
			}

			if (ba_reader->HasNext()) {
				int64_t batch_size =
				    index_vector[1] - index_vector[0] + 1;
				std::vector<parquet::ByteArray> values(
				    batch_size);
				std::vector<int16_t> definition_levels(batch_size); // Use a vector for definition levels
				parquet::ByteArray value;
				rows_read = ba_reader->ReadBatch(batch_size,
				    definition_levels.data(), nullptr, values.data(),
				    &values_read);
				if (batch_size == rows_read &&
				    batch_size == values_read) {
					for (int64_t b = 0; b < batch_size;
					     b++) {
						parquet_data_packet *pack =
						    (parquet_data_packet *)
						        malloc(sizeof(
						            parquet_data_packet));
						if (!pack) {
							log_error("Memory allocation failed for parquet_data_packet");
							for (auto p : ret_vec) {
								free(p->data);
								free(p);
							}
							return vector<parquet_data_packet *>();
						}
						pack->data =
						    (uint8_t *) malloc(
						        values[b].len *
						        sizeof(uint8_t));
						memcpy(pack->data,
						    values[b].ptr,
						    values[b].len);
						pack->size = values[b].len;
						ret_vec.push_back(pack);
					}
				}
			}

		}

	} catch (const std::exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return ret_vec;
}

typedef enum {
	START_KEY,
	END_KEY
} key_type;

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

parquet_data_packet **
parquet_find_data_span_packets(conf_parquet *conf, uint64_t start_key, uint64_t end_key, uint32_t *size, char *topic)
{
	vector<parquet_data_packet *> ret_vec;
	parquet_data_packet         **packets = NULL;
	uint32_t                      len     = 0;

	const char **filenames = parquet_find_span(start_key, end_key, &len);

	for (uint32_t i = 0; i < len; i++) {
		if (strstr(filenames[i], topic) == NULL) {
			nng_strfree((char*)filenames[i]);
			continue;
		}

		uint64_t keys[2];
		keys[0]  = start_key;
		keys[1]  = end_key;
		if (len > 1) {
			keys[0] = i == 0 ? start_key
			                 : get_key(filenames[i], START_KEY);
			keys[1] = i == (len - 1)
			    ? end_key
			    : get_key(filenames[i], END_KEY);
		}

		auto tmp = parquet_read_span(conf, filenames[i], keys);
		ret_vec.insert(ret_vec.end(), tmp.begin(), tmp.end());
		nng_strfree((char*)filenames[i]);
	}
	nng_free(filenames, len);

	if (!ret_vec.empty()) {
		packets = (parquet_data_packet **) malloc(
		    sizeof(parquet_data_packet *) * ret_vec.size());
		copy(ret_vec.begin(), ret_vec.end(), packets);
		*size = ret_vec.size();
	}

	return packets;
}
