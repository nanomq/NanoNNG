#ifndef PARQUET_H
#define PARQUET_H
#include "nng/nng.h"
#include "nng/supplemental/nanolib/conf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint64_t *keys;
	uint8_t **darray;
	uint32_t *dsize;
	uint32_t  size;
	nng_aio  *aio;
	void     *arg;
} parquet_object;

parquet_object *parquet_object_alloc(uint64_t *keys, uint8_t **darray,
    uint32_t *dsize, uint32_t size, nng_aio *aio, void *arg);
int             parquet_write_batch_async(parquet_object *elem);
int             parquet_write_launcher(conf_parquet *conf);

const char  *parquet_find(uint64_t key);
const char **parquet_find_span(uint64_t key, uint32_t offset, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
