#ifndef PARQUET_H
#define PARQUET_H
#include "nng/nng.h"
#include "nng/supplemental/nanolib/conf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t *keys;
	uint8_t **darray;
	uint32_t *dsize;
	uint32_t  size;
	nng_aio  *aio;
} parquet_object;

parquet_object *parquet_object_alloc(uint32_t *keys, uint8_t **darray,
    uint32_t *dsize, uint32_t size, nng_aio *aio);
int             parquet_write_batch_async(parquet_object *elem);
int             parquet_write_launcher(conf_parquet *conf);

const char  *parquet_find(uint32_t key);
const char **parquet_find_span(uint32_t key, uint32_t offset, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
