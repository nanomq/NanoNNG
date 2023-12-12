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
