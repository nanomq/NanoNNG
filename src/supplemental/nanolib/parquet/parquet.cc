#include <arrow/io/file.h>
#include <parquet/stream_writer.h>
#include <parquet/stream_reader.h>

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include "nng/supplemental/nanolib/parquet.h"
#include "nng/supplemental/nanolib/log.h"
#include "queue.h"
#include <assert.h>

using namespace std;
using parquet::ConvertedType;
using parquet::Repetition;
using parquet::Type;
using parquet::schema::GroupNode;
using parquet::schema::PrimitiveNode;

#define DO_IT_IF_NOT_NULL(func, arg1, arg2) \
    if (arg1) { \
        func(arg1, arg2);\
    }

#define FREE_IF_NOT_NULL(free, size) \
    DO_IT_IF_NOT_NULL(nng_free, free, size)

CircularQueue parquet_queue;
pthread_mutex_t parquet_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t parquet_queue_not_empty = PTHREAD_COND_INITIALIZER;

static bool directory_exists(const std::string& directory_path) {
    struct stat buffer;
    return (stat(directory_path.c_str(), &buffer) == 0 && S_ISDIR(buffer.st_mode));
}

static bool create_directory(const std::string& directory_path) {
    int status = mkdir(directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    return (status == 0);
}

static char *get_file_name(parquet_conf *conf)
{
    char *dir = conf->dir;
    char *prefix = conf->file_name_prefix;
    uint8_t index = conf->file_index++;
    if (index > conf->file_count) {
        log_error("file count exceeds");
        return NULL;
    }

    char *file_name = (char *)malloc(strlen(prefix) + strlen(dir) + 8);
    sprintf(file_name, "%s/%s%d.parquet",dir, prefix, index);

    return file_name;
}

static shared_ptr<GroupNode> setup_schema()
{
    parquet::schema::NodeVector fields;
    fields.push_back(parquet::schema::PrimitiveNode::Make(
        "key", parquet::Repetition::OPTIONAL, parquet::Type::INT32,
        parquet::ConvertedType::UINT_32));
    fields.push_back(parquet::schema::PrimitiveNode::Make(
        "data", parquet::Repetition::OPTIONAL, parquet::Type::BYTE_ARRAY,
        parquet::ConvertedType::UTF8));
 
    return static_pointer_cast<GroupNode>(
      GroupNode::Make("schema", Repetition::REQUIRED, fields));
}

parquet_object *parquet_object_alloc(uint32_t *keys, uint8_t **darray, uint32_t *dsize, uint32_t size, nng_aio *aio)
{
    parquet_object * elem = new parquet_object;
    elem->keys = keys;
    elem->darray = darray;
    elem->dsize = dsize;
    elem->size = size;
    elem->aio = aio;
    return elem;
}

void parquet_object_free(parquet_object *elem)
{
    if (elem) {
        FREE_IF_NOT_NULL(elem->keys, elem->size);
        FREE_IF_NOT_NULL(elem->darray, elem->size);
        FREE_IF_NOT_NULL(elem->dsize, elem->size);
        DO_IT_IF_NOT_NULL(nng_aio_finish, elem->aio, 0);
        delete elem;
    }
}

int
parquet_write_batch_async(parquet_object *elem)
{
	pthread_mutex_lock(&parquet_queue_mutex);
	if (IS_EMPTY(parquet_queue)) {
		pthread_cond_broadcast(&parquet_queue_not_empty);
	}
	ENQUEUE(parquet_queue, elem);
	log_debug("enqueue element.");
	puts("enqueue element.");
	pthread_mutex_unlock(&parquet_queue_mutex);

	return 0;
}

int parquet_write(std::shared_ptr<parquet::ParquetFileWriter> file_writer, parquet_object *elem)
{

    // Append a RowGroup with a specific number of rows.
    parquet::RowGroupWriter *rg_writer = file_writer->AppendRowGroup();

    // Write the Int32 column
    parquet::Int32Writer    *int32_writer =
	static_cast<parquet::Int32Writer *>(rg_writer->NextColumn());
    for (int i = 0; i < elem->size; i++) {
      int32_t value = elem->keys[i];
      int16_t definition_level = 1;
      cout << value << endl;
      int32_writer->WriteBatch(1, &definition_level, nullptr, &value);
    }

    // Write the ByteArray column. Make every alternate values NULL
    parquet::ByteArrayWriter *ba_writer =
	static_cast<parquet::ByteArrayWriter *>(rg_writer->NextColumn());
    for (int i = 0; i < elem->size; i++) {
	    parquet::ByteArray value;
	    int16_t            definition_level = 1;
	    value.ptr                           = elem->darray[i];
	    value.len                           = elem->dsize[i];
        cout << value.ptr << value.len << endl;
	    ba_writer->WriteBatch(1, &definition_level, nullptr, &value);
    }

    parquet_object_free(elem);
    return 0;
}


void parquet_write_loop(void *config)
{
    if (config == NULL) {
        log_error("parquet conf is NULL");
    }

    parquet_conf *conf = (parquet_conf*) config;
    if (!directory_exists(conf->dir)) {
	    if (!create_directory(conf->dir)) {
		    log_error("Failed to create directory %s", conf->dir);
		    return;
	    }
    }

    INIT_QUEUE(parquet_queue);

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
	    puts("fetch element from parquet queue");
	    parquet_object *ele = (parquet_object *) DEQUEUE(parquet_queue);
	    pthread_mutex_unlock(&parquet_queue_mutex);
	    parquet_write(file_writer, ele);
        
        // Close the ParquetFileWriter
        // check file size if necessary and open new one
        // file_writer->Close();

        // Write the bytes to file
        // DCHECK(out_file->Close().ok());
    }

    return;
}

int
parquet_write_launcher(parquet_conf *conf)
{
	nng_thread *thread;
	INIT_QUEUE(parquet_queue);
	nng_thread_create(&thread, parquet_write_loop, (void*) conf);
}