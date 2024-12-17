
#include "parquet_file_manager.h"

void
parquet_file_manager::add_queue(conf_exchange_node *node)
{
	file_queue_map[node->name] = make_shared<parquet_file_queue>(node->parquet);
	file_queue_map[node->name]->init();
}
