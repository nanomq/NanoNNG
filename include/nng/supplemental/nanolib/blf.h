#ifndef BLF_H
#define BLF_H
#include "nng/nng.h"
#include "nng/supplemental/nanolib/conf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blf_object blf_object;
typedef void (*blf_cb)(blf_object *arg);

// typedef enum {
// 	WRITE_TO_NORMAL,
// 	WRITE_TO_TEMP,
// } blf_write_type;


typedef enum {
	WRITE_TO_NORMAL1,
	WRITE_TO_TEMP1,
} blf_write_type;

typedef struct {
	uint32_t start_idx;
	uint32_t end_idx;
	char    *filename;
} blf_file_range;

typedef struct {
	blf_file_range **range;
	int                  size;
	int                  start; // file range start index
} blf_file_ranges;

typedef struct {
	uint8_t *data;
	uint32_t size;
} blf_data_packet;

struct blf_object {
	uint64_t            *keys;
	uint8_t            **darray;
	uint32_t            *dsize;
	uint32_t             size;
	nng_aio             *aio;
	void	        *arg;
	blf_file_ranges *ranges;
	blf_write_type   type;
};

blf_object *blf_object_alloc(uint64_t *keys, uint8_t **darray,
    uint32_t *dsize, uint32_t size, nng_aio *aio, void *arg);
void            blf_object_free(blf_object *elem);

// blf_file_range *blf_file_range_alloc(uint32_t start_idx, uint32_t end_idx, char *filename);
// void blf_file_range_free(blf_file_range *range);
// 
// void blf_object_set_cb(blf_object *obj, blf_cb cb);
int  blf_write_batch_async(blf_object *elem);
// Write a batch to a temporary blf file, utilize it in scenarios where a single 
// file is sufficient for writing, sending, and subsequent deletion.
// int  blf_write_batch_tmp_async(blf_object *elem);
int  blf_write_launcher(conf_blf *conf);

const char  *blf_find(uint64_t key);
const char **blf_find_span(
    uint64_t start_key, uint64_t end_key, uint32_t *size);
 
// blf_data_packet *blf_find_data_packet(conf_blf *conf, char *filename, uint64_t key);
// 
// blf_data_packet **blf_find_data_packets(conf_blf *conf, char **filenames, uint64_t *keys, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
