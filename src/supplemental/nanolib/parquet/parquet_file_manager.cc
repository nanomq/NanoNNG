
#include "parquet_file_manager.h"

void
parquet_file_manager::add_queue(conf_exchange_node *node)
{
	file_queue_map[node->topic] = make_shared<parquet_file_queue>(node->parquet);
	file_queue_map[node->topic]->init();
}

void
parquet_file_manager::remove_queue()
{
}

CircularQueue *
parquet_file_manager::fetch_queue(const string &topic)
{

    auto it = file_queue_map.find(topic);
    if (it != file_queue_map.end()) {
        return it->second->get_queue();
    } else {
        return nullptr;
    }
}

conf_parquet*
parquet_file_manager::fetch_conf(const string &topic)
{
    return file_queue_map[topic]->get_conf();
}


void
parquet_file_manager::update_queue(
    const string &topic, const char *md5_file_name)
{
	file_queue_map[topic]->update_queue(md5_file_name);
}

uint64_t
parquet_file_manager::get_queue_sum(const string &topic)
{
    return file_queue_map[topic]->get_sum();
}