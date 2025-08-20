#include"parquet_file_queue.h"
#include <unistd.h>

#include <unistd.h>
#include <sys/stat.h>

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

			log_debug("Loaded %zu parquet file %s, %p in time "
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
	log_debug("queue size: %d, file_count: %d", QUEUE_SIZE(queue),
	    node->file_count);

	struct stat st;
	if (stat(filename, &st) == 0) {
		uint64_t file_size = (uint64_t) st.st_size;
		sum += file_size;

		while (sum > node->file_size) {
			remove_old_file(queue);
		}

		if (QUEUE_SIZE(queue) > node->file_count) {
			remove_old_file(queue);
		}
	}
}

conf_parquet *
parquet_file_queue::get_conf()
{
	return node;
}

optional<long>
parquet_file_queue::extract_start_time(const std::string &file_name)
{
	regex  pattern("-([0-9]+)~");
	smatch matches;

	try {
		if (regex_search(file_name, matches, pattern) &&
		    matches.size() > 1) {
			return stol(matches[1].str());
		}
	} catch (const std::exception &e) {
		log_error("Error extracting start time: %s", e.what());
	}

	return std::nullopt;
}

bool
parquet_file_queue::is_parquet_file(const string &file_name)
{
	const string PARQUET_EXTENSION = ".parquet";
	if (file_name.length() < PARQUET_EXTENSION.length()) {
		return false;
	}
	return file_name.compare(
	           file_name.length() - PARQUET_EXTENSION.length(),
	           PARQUET_EXTENSION.length(), PARQUET_EXTENSION) == 0;
}

bool
parquet_file_queue::has_md5_sum(const string &file_name)
{
	return file_name.find("_") != string::npos;
}


bool
parquet_file_queue::directory_exists(const std::string &directory_path)
{
	struct stat buffer;
	return (stat(directory_path.c_str(), &buffer) == 0 &&
	    S_ISDIR(buffer.st_mode));
}

bool
parquet_file_queue::create_directory(const std::string &directory_path)
{
	int status = mkdir(
	    directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	return (status == 0);
}

int
parquet_file_queue::remove_old_file(CircularQueue &queue)
{
	int   ret      = 0;
	char *filename = (char *) DEQUEUE(queue);

	struct stat st;
	if (stat(filename, &st) == 0) {
		uint64_t file_size = (uint64_t) st.st_size;
		sum -= file_size;
	}

	if (remove(filename) == 0) {
		log_debug("File '%s' removed successfully.\n", filename);
	} else {
		log_error(
		    "Error removing the file %s errno: %d", filename, errno);
		ret = -1;
	}

	free(filename);
	return ret;
}

uint64_t
parquet_file_queue::get_sum()
{
	return sum;
}