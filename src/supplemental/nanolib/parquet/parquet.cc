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
