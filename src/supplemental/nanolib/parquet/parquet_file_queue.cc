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

// Initialize queue
void
parquet_file_queue::init()
{
	DIR                *dir;
	struct dirent      *ent;
	vector<ParquetFile> files;
	conf_parquet       *parquet = node;

	if ((dir = opendir(parquet->dir)) != nullptr) {
		while ((ent = readdir(dir)) != nullptr) {
			string file_name(ent->d_name);

			// Check for valid parquet file
			if (is_parquet_file(file_name)) {
				string file_path =
				    string(parquet->dir) + "/" + file_name;

				if (!has_md5_sum(file_name)) {
					if (unlink(file_path.c_str()) != 0) {
						log_error("Failed to remove "
						          "file %s, errno: %d",
						    file_path.c_str(), errno);
					} else {
						log_warn("Deleted file "
						         "without md5sum: %s",
						    file_path.c_str());
					}
					continue;
				}

				auto start_time =
				    extract_start_time(file_name);
				if (!start_time.has_value()) {
					log_error("Failed to extract start "
					          "time from file: %s",
					    file_name.c_str());
					continue;
				}

				files.push_back({ file_path, start_time.value() });
			}
		}
		closedir(dir);

		// Sort files by start time
		sort(files.begin(), files.end(),
		    [](const ParquetFile &a, const ParquetFile &b) {
			    return a.start_time < b.start_time;
		    });

		// Enqueue sorted files
		for (const auto &file : files) {
			char *file_name = strdup(file.file_path.c_str());
			update_queue(file_name);

			log_info("Loaded %zu parquet file %s, %p in time "
			         "order from %s.",
			    files.size(), parquet->dir, file_name, file_name);
		}
	} else {
		log_info("Parquet directory not found, creating new one.");

		if (!directory_exists(parquet->dir)) {
			if (!create_directory(parquet->dir)) {
				log_error("Failed to create directory %s",
				    parquet->dir);
				return;
			}
		}
	}
}

CircularQueue *
parquet_file_queue::get_queue()
{
	return &queue;
}

void
parquet_file_queue::update_queue(const char *filename)
{
	ENQUEUE(queue, (void *) filename);
	log_info("queue size: %d, file_count: %d", QUEUE_SIZE(queue),
	    node->file_count);
	if (QUEUE_SIZE(queue) > node->file_count) {
		remove_old_file(queue);
	}
}
