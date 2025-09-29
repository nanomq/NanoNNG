#ifndef PARQUET_FILE_MANAGER_H
#define PARQUET_FILE_MANAGER_H

#include "parquet_file_queue.h"

#include <unordered_map>

// Make it default inline
class parquet_file_manager {
    public:
	void update_queue(const string &topic, const char *filename)
	{
		auto it = file_queue_map.find(topic);
		if (it != file_queue_map.end()) {
			it->second->update_queue(filename);
		}
	}
	void add_queue(conf_exchange_node *node)
	{
		file_queue_map[node->topic] =
		    make_shared<parquet_file_queue>(node->parquet);
		file_queue_map[node->topic]->init();
	}
	CircularQueue *fetch_queue(const string &topic) const
	{
		auto           it    = file_queue_map.find(topic);
		CircularQueue *queue = nullptr;
		if (it != file_queue_map.end()) {
			queue = it->second->get_queue();
		}
		return queue;
	}

	conf_parquet *fetch_conf(const string &topic) const
	{
		auto          it   = file_queue_map.find(topic);
		conf_parquet *conf = nullptr;
		if (it != file_queue_map.end()) {
			conf = it->second->get_conf();
		}
		return conf;
		;
	}

	uint64_t get_queue_sum(const string &topic) const
	{
		auto     it  = file_queue_map.find(topic);
		uint64_t sum = 0;
		if (it != file_queue_map.end()) {
			sum = it->second->get_sum();
		}
		return sum;
	}

	uint32_t get_queue_index(const string &topic) const
	{
		auto     it    = file_queue_map.find(topic);
		uint32_t index = 0;
		if (it != file_queue_map.end()) {
			index = it->second->get_index();
		}
		return index;
	}
	void remove_queue() { };

    private:
	unordered_map<string, shared_ptr<parquet_file_queue>> file_queue_map;
};

#endif // PARQUET_FILE_MANAGER_H