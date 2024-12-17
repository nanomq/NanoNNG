
#include "parquet_file_manager.h"

void
parquet_file_manager::add_queue(conf_exchange_node *node)
{
	file_queue_map[node->name] = make_shared<parquet_file_queue>(node->parquet);
	file_queue_map[node->name]->init();
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