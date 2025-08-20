#ifndef PARQUET_FILE_MANAGER_H
#define PARQUET_FILE_MANAGER_H

#include "parquet_file_queue.h"

#include <unordered_map>

class parquet_file_manager {
    public:
	void update_queue(const string &topic, const char *filename);
	void add_queue(conf_exchange_node *node);
    CircularQueue* fetch_queue(const string &topic);
	conf_parquet* fetch_conf(const string &topic);
	uint64_t get_queue_sum(const string &topic);
	void remove_queue();

    private:
	unordered_map<string, shared_ptr<parquet_file_queue>>
	    file_queue_map;
};

#endif // PARQUET_FILE_MANAGER_H