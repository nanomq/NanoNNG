#include"parquet_file_queue.h"
#include <unistd.h>

// Constructor
parquet_file_queue::parquet_file_queue(conf_parquet *node)
    : node(node)
{
	INIT_QUEUE(queue);
}
