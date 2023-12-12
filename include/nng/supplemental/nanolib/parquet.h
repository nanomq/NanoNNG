#ifndef PARQUET_H
#define PARQUET_H
#include "nng/nng.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	UNCOMPRESSED,
	SNAPPY,
	GZIP,
	BROTLI,
	ZSTD,
	LZ4
} compression_type;

typedef struct {
	char            *dir;
	char            *file_name_prefix;
	compression_type comp_type;
	uint8_t          file_count;
	uint8_t          file_index;
	int32_t          file_size;
} parquet_conf;

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
int             parquet_write_launcher(parquet_conf *conf);

// TODO: unsupport now
char  *parquetFind(uint32_t key);
char **parquetFindSpan(uint32_t key, uint32_t span);

#ifdef __cplusplus
}
#endif

#endif