#include"parquet_file_queue.h"

// Constructor
parquet_file_queue::parquet_file_queue(conf_parquet *node)
    : node(node)
{
	INIT_QUEUE(queue);
}

// Destructor
parquet_file_queue::~parquet_file_queue()
{
	while (!IS_EMPTY(queue)) {
		char *name = (char *) DEQUEUE(queue);
		free(name);
	}
	DESTROY_QUEUE(queue);
}
