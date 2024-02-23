#include <Vector/BLF.h>
#include <codecvt>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <locale>
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/blf.h"
#include "queue.h"
#include <assert.h>
#include <atomic>
#include <dirent.h>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
using namespace std;

#define UINT64_MAX_DIGITS 20
#define _Atomic(X) std::atomic<X>
atomic_bool is_available = {false};
#define WAIT_FOR_AVAILABLE    \
	while (!is_available) \
		nng_msleep(10);
static conf_blf *g_conf = NULL;

#define DO_IT_IF_NOT_NULL(func, arg1, arg2) \
	if (arg1) {                         \
		func(arg1, arg2);           \
	}

#define FREE_IF_NOT_NULL(free, size) DO_IT_IF_NOT_NULL(nng_free, free, size)


CircularQueue   blf_queue;
CircularQueue   blf_file_queue;
pthread_mutex_t blf_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  blf_queue_not_empty = PTHREAD_COND_INITIALIZER;


static bool
directory_exists(const std::string &directory_path)
{
	struct stat buffer;
	return (stat(directory_path.c_str(), &buffer) == 0 &&
	    S_ISDIR(buffer.st_mode));
}

static bool
create_directory(const std::string &directory_path)
{
	int status = mkdir(
	    directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	return (status == 0);
}

blf_file_range *
blf_file_range_alloc(uint32_t start_idx, uint32_t end_idx, char *filename)
{
	blf_file_range *range = new blf_file_range;
	range->start_idx          = start_idx;
	range->end_idx            = end_idx;
	range->filename           = nng_strdup(filename);
	return range;
}

void
blf_file_range_free(blf_file_range *range)
{
	if (range) {
		FREE_IF_NOT_NULL(
		    range->filename, strlen(range->filename));
		delete range;
	}
}

blf_object *
blf_object_alloc(uint64_t *keys, uint8_t **darray, uint32_t *dsize,
    uint32_t size, nng_aio *aio, void *arg)
{
	blf_object *elem = new blf_object;
	elem->keys           = keys;
	elem->darray         = darray;
	elem->dsize          = dsize;
	elem->size           = size;
	elem->aio            = aio;
	elem->arg            = arg;
	elem->ranges         = new blf_file_ranges;
	elem->ranges->range  = NULL;
	elem->ranges->start  = 0;
	elem->ranges->size   = 0;
	return elem;
}

void
blf_object_free(blf_object *elem)
{
	if (elem) {
		FREE_IF_NOT_NULL(elem->keys, elem->size);
		FREE_IF_NOT_NULL(elem->dsize, elem->size);
		nng_aio_set_prov_data(elem->aio, elem->arg);
		nng_aio_set_output(elem->aio, 1, elem->ranges);
		uint32_t *szp = (uint32_t *) malloc(sizeof(uint32_t));
		*szp          = elem->size;
		nng_aio_set_msg(elem->aio, (nng_msg *) szp);
		DO_IT_IF_NOT_NULL(nng_aio_finish_sync, elem->aio, 0);
		FREE_IF_NOT_NULL(elem->darray, elem->size);
		for (int i = 0; i < elem->ranges->size; i++) {
			blf_file_range_free(elem->ranges->range[i]);
		}
		free(elem->ranges->range);
		delete elem->ranges;
		delete elem;
	}
}


static char *
get_file_name(conf_blf *conf, uint64_t key_start, uint64_t key_end)
{
	char *file_name = NULL;
	char *dir       = conf->dir;
	char *prefix    = conf->file_name_prefix;

	file_name = (char *) malloc(strlen(prefix) + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}

	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".blf", dir,
	    prefix, key_start, key_end);
	ENQUEUE(blf_file_queue, file_name);
	return file_name;
}

static int
compute_new_index(blf_object *obj, uint32_t index, uint32_t file_size)
{
	uint64_t size = 0;
	uint32_t new_index;
	for (new_index = index; size < file_size && new_index < obj->size - 1;
	     new_index++) {
		size += obj->dsize[new_index];
	}
	return new_index;
}


static int
remove_old_file(void)
{
	char *filename = (char *) DEQUEUE(blf_file_queue);
	if (remove(filename) == 0) {
		log_debug("File '%s' removed successfully.\n", filename);
	} else {
		log_error("Error removing the file %s", filename);
		return -1;
	}

	free(filename);
	return 0;
}

void
update_blf_file_ranges(
    conf_blf *conf, blf_object *elem, blf_file_range *range)
{
	if (elem->ranges->size != conf->file_count) {
		elem->ranges->range =
		    (blf_file_range **) realloc(elem->ranges->range,
		        sizeof(blf_file_range *) * (++elem->ranges->size));
		elem->ranges->range[elem->ranges->size - 1] = range;
	} else {
		// Free old ranges and insert new ranges
		// update start index
		blf_file_range_free(
		    elem->ranges->range[elem->ranges->start]);
		elem->ranges->range[elem->ranges->start] = range;
		elem->ranges->start++;
		elem->ranges->start %= elem->ranges->size;
	}
}

#define hocon_read_num_base(structure, field, key, jso)                 \
	do {                                                            \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);         \
		if (NULL == jso_key) {                                  \
			log_debug("Config %s is not set, use default!", key);       \
			break;                                          \
		}                                                       \
		if (cJSON_IsNumber(jso_key)) {                          \
			if (jso_key->valuedouble > 0)                      \
				(structure)->field = jso_key->valuedouble; \
		}                                                       \
	} while (0);


// {"frames":[{"id":387,"bus":8,"d":0,"t":16218652,"data":"43A000001000"},{"id":1681,"bus":11,"d":0,"t":16218654,"data":"78050A058F11E0C7"},{"id":469,"bus":8,"d":0,"t":16218661,"data":"800080005400052F"},{"id":1527,"bus":8,"d":0,"t":16218663,"data":"00138C62407FFEF0"},{"id":474,"bus":11,"d":0,"t":16218665,"data":"7FFE801861D82750"}],"ts":1708584620370}
// jso is single frame
void blf_write_can_message(Vector::BLF::File &file, cJSON *jso)
{
    /* write a CanMessage */
    auto * canMessage = new Vector::BLF::CanMessage;
    hocon_read_num_base(canMessage, id, "id", jso);
    hocon_read_num_base(canMessage, objectTimeStamp, "t", jso);

    hocon_read_num_base(canMessage, channel, "bus", jso);
    hocon_read_num_base(canMessage, flags, "d", jso);
    hocon_read_num_base(canMessage, dlc, "l", jso);
    // TODO: data 直接传数组过来，是不是好一点

    // canMessage->id = 0x33;
    // canMessage->objectTimeStamp = 16218665;
    // canMessage->channel = 1;
    // canMessage->flags = 1; // TX
    // canMessage->dlc = 2;
    // canMessage->data[0] = 0x44;
    // canMessage->data[1] = 0x55;
    file.write(canMessage);

}


int blf_write_core(char *name, blf_object *elem, uint32_t old_index, uint32_t new_index)
{
    /* open file for writing */
    Vector::BLF::File file;
    // file.open("test.blf", std::ios_base::out);
    file.open(name, std::ios_base::out);
    if (!file.is_open()) {
        std::cout << "Unable to open file" << std::endl;
        return -1;
    }

    for (uint32_t i = old_index; i <= new_index; i++) {

        cJSON *jso = cJSON_ParseWithLength((const char*) elem->darray[i], elem->dsize[i]);

        /* write a CanMessage */
        blf_write_can_message(file, jso);
    }

    /* close file */
    file.close();
    return 0;
}

int
blf_write(
    conf_blf *conf, blf_object *elem)
{
	uint32_t old_index = 0;
	uint32_t new_index = 0;
again:

	new_index = compute_new_index(elem, old_index, conf->file_size);
	uint64_t key_start = elem->keys[old_index];
	uint64_t key_end   = elem->keys[new_index];
	pthread_mutex_lock(&blf_queue_mutex);
	char *filename = get_file_name(conf, key_start, key_end);
	if (filename == NULL) {
		pthread_mutex_unlock(&blf_queue_mutex);
		log_error("Failed to get file name");
		return -1;
	}

	if (QUEUE_SIZE(blf_file_queue) > conf->file_count) {
		remove_old_file();
	}
	pthread_mutex_unlock(&blf_queue_mutex);

	{
		blf_file_range *range = blf_file_range_alloc(old_index, new_index, filename);
		update_blf_file_ranges(conf, elem, range);
        // TODO here

        // write key?
		// for (uint32_t i = old_index; i <= new_index; i++) {
		// 	int64_t value            = elem->keys[i];
		// 	int16_t definition_level = 1;
		// 	int64_writer->WriteBatch(
		// 	    1, &definition_level, nullptr, &value);
		// }

        // write value
        blf_write_core(filename, elem, old_index, new_index);
		// for (uint32_t i = old_index; i <= new_index; i++) {

			// parquet::ByteArray value;
			// int16_t            definition_level = 1;
			// value.ptr                           = elem->darray[i];
			// value.len                           = elem->dsize[i];
			// ba_writer->WriteBatch(
			//     1, &definition_level, nullptr, &value);
		// }

        


		old_index = new_index;

		if (new_index != elem->size - 1)
			goto again;
	}

	blf_object_free(elem);
	return 0;
}



void
blf_write_loop(void *config)
{
	if (config == NULL) {
		log_error("blf conf is NULL");
	}

	conf_blf *conf = (conf_blf *) config;
	if (!directory_exists(conf->dir)) {
		if (!create_directory(conf->dir)) {
			log_error("Failed to create directory %s", conf->dir);
			return;
		}
	}


	while (true) {
		// wait for mqtt messages to send method request
		pthread_mutex_lock(&blf_queue_mutex);

		while (IS_EMPTY(blf_queue)) {
			pthread_cond_wait(
			    &blf_queue_not_empty, &blf_queue_mutex);
		}

		log_debug("fetch element from blf queue");
		blf_object *ele =
		    (blf_object *) DEQUEUE(blf_queue);

		pthread_mutex_unlock(&blf_queue_mutex);

		switch (ele->type) {
		case WRITE_TO_NORMAL:
			blf_write(conf, ele);
			break;
		case WRITE_TO_TEMP:
			// blf_write_tmp(conf, schema, ele);
			break;
		default:
			break;
		}

	}
}


int
blf_write_batch_async(blf_object *elem)
{
	elem->type = WRITE_TO_NORMAL;
	WAIT_FOR_AVAILABLE
	pthread_mutex_lock(&blf_queue_mutex);
	if (IS_EMPTY(blf_queue)) {
		pthread_cond_broadcast(&blf_queue_not_empty);
	}
	ENQUEUE(blf_queue, elem);
	log_debug("enqueue element.");

	pthread_mutex_unlock(&blf_queue_mutex);
	return 0;
}

int
blf_write_launcher(conf_blf *conf)
{
	g_conf = conf;
	INIT_QUEUE(blf_queue);
	INIT_QUEUE(blf_file_queue);
	is_available = true;
	thread write_loop(blf_write_loop, conf);
	write_loop.detach();
	return 0;
}