#ifndef PARQUET_H
#define PARQUET_H
#include "nng/nng.h"
#include "nng/supplemental/nanolib/conf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct parquet_object parquet_object;
typedef void (*parquet_cb)(parquet_object *arg);

#define PARQUET_WRITE_SUCCESS 1
#define PARQUET_WRITE_FAILURE 0

typedef struct {
	uint32_t start_idx;
	uint32_t end_idx;
	char    *filename;
} parquet_file_range;

typedef struct {
	parquet_file_range **range;
	int                  size;
} parquet_file_ranges;

struct parquet_object {
	uint64_t            *keys;
	uint8_t            **darray;
	uint32_t            *dsize;
	uint32_t             size;
	nng_aio             *aio;
	void	        	*arg;
	parquet_file_ranges *ranges;
};

parquet_object *parquet_object_alloc(uint64_t *keys, uint8_t **darray,
    uint32_t *dsize, uint32_t size, nng_aio *aio, void *arg);
void            parquet_object_free(parquet_object *elem);

void parquet_object_set_cb(parquet_object *obj, parquet_cb cb);
int  parquet_write_batch_async(parquet_object *elem);
int  parquet_write_launcher(conf_parquet *conf);

const char  *parquet_find(uint64_t key);
const char **parquet_find_span(
    uint64_t start_key, uint64_t end_key, uint32_t *size);


#ifdef __cplusplus
}
#endif

#endif
