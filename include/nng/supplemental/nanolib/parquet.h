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

struct parquet_object {
	uint64_t  *keys;
	uint8_t  **darray;
	uint32_t  *dsize;
	uint32_t   size;
	nng_aio   *aio;
	void      *arg;
	int        result;
	int        file_size;
	char     **file_list;
	parquet_cb cb;
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

nng_msg *parquet_find_msg(char *filename, uint64_t key);

#ifdef __cplusplus
}
#endif

#endif
