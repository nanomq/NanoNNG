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
	vector<ParquetFile> files_start_time, files_seq_id;
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

				//
				auto file_seq_id = extract_seq_id(file_name);
				if (!file_seq_id.has_value()) {
					auto start_time =
					    extract_start_time(file_name);

					// If start time extraction fails, skip
					// the file
					if (!start_time.has_value()) {
						log_error(
						    "Failed to extract start "
						    "time from file: %s",
						    file_name.c_str());
						continue;
					}
					files_start_time.push_back(
					    { file_path, start_time.value() });
					continue;
				}

				files_seq_id.push_back(
				    { file_path, file_seq_id.value() });
			}
		}
		closedir(dir);

		// Sort files by start time
		sort(files_start_time.begin(), files_start_time.end(),
		    [](const ParquetFile &a, const ParquetFile &b) {
			    return a.order_key < b.order_key;
		    });

		// Step1: Sort files by seq id
		sort(files_seq_id.begin(), files_seq_id.end(),
		    [](const ParquetFile &a, const ParquetFile &b) {
			    return a.order_key < b.order_key;
		    });

		// Step 2: find first discontinuity
		size_t break_pos = files_seq_id.size();
		for (size_t i = 0; i + 1 < files_seq_id.size(); ++i) {
			if (files_seq_id[i + 1].order_key !=
			    files_seq_id[i].order_key + 1) {
				break_pos = i + 1; // next element is where
				                   // discontinuity starts
				break;
			}
		}

		// Step 3: if found discontinuity, rotate vector
		if (break_pos < files_seq_id.size()) {
			std::rotate(files_seq_id.begin(),
			    files_seq_id.begin() + break_pos,
			    files_seq_id.end());
		}

		// Enqueue sorted files
		for (const auto &file : files_start_time) {
			char *file_name = strdup(file.file_path.c_str());
			update_queue(file_name);

			log_warn("Loaded %zu parquet file %s, %p in time "
			         "order from %s.",
			    files_start_time.size(), parquet->dir, file_name,
			    file_name);
		}

		for (const auto &file : files_seq_id) {
			char *file_name = strdup(file.file_path.c_str());
			update_queue(file_name);
			std::cout << "file.order_key: " << file.order_key
			          << std::endl;
			set_index(file.order_key + 1);
			log_warn("Loaded %zu parquet file %s, %p in time "
			         "order from %s.",
			    files_seq_id.size(), parquet->dir, file_name,
			    file_name);
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


optional<long>
parquet_file_queue::extract_seq_id(const std::string &file_name)
{
	regex  pattern("_([0-9]+)_");
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