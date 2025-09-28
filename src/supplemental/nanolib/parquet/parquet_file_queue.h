#ifndef PARQUET_FILE_QUEUE_H
#define PARQUET_FILE_QUEUE_H

#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/parquet.h"
#include "nng/supplemental/nanolib/queue.h"

#include <optional>
#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <vector>

using namespace std;

struct ParquetFile {
	string file_path;
	long   start_time;
};

class parquet_file_queue {
    public:
	explicit parquet_file_queue(conf_parquet *node);
	~parquet_file_queue();
	void           init();
	void           update_queue(const char *filename);
	int            remove_old_file(CircularQueue &queue);
	// Make it inline
	conf_parquet  *get_conf() const { return node; }
	CircularQueue *get_queue() const { return &queue; }
	uint64_t       get_sum() const { return sum; }
	uint32_t       get_index() const { return index; }

    private:
	static optional<long> extract_start_time(const string &file_name);
	static int  compare_files(const ParquetFile &a, const ParquetFile &b);
	static bool directory_exists(const string &directory_path);
	static bool create_directory(const string &directory_path);
	static bool is_parquet_file(const string &file_name);
	static bool has_md5_sum(const string &file_name);

	conf_parquet *node;
	CircularQueue queue;
	uint32_t      index = 0;
	uint64_t      sum   = 0;
};

#endif // PARQUET_FILE_QUEUE_H
