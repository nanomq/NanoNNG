#ifndef PARQUET_H
#define PARQUET_H
#include "nng/nng.h"
#include "nng/supplemental/nanolib/conf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct parquet_object   parquet_object;
typedef struct parquet_data     parquet_data;
typedef struct parquet_data_ret parquet_data_ret;
typedef struct parquet_payload  parquet_payload;
typedef void (*parquet_cb)(parquet_object *arg);

typedef enum {
	WRITE_RAW,
	WRITE_CAN,
	WRITE_TEMP_RAW,
} parquet_type;

typedef struct {
	uint32_t start_idx;
	uint32_t end_idx;
	char    *filename;
} parquet_file_range;

typedef struct {
	parquet_file_range **range;
	int                  size;
	int                  start; // file range start index
} parquet_file_ranges;

typedef struct {
	uint8_t *data;
	uint32_t size;
} parquet_data_packet;


struct parquet_data_ret {
	// Payload_arr should col first.
	uint32_t               col_len;
	uint32_t               row_len;
	uint64_t              *ts;
	char                 **schema;
	parquet_data_packet ***payload_arr;
};


struct parquet_object {
	parquet_data        *data;
	parquet_type         type;
	nng_aio             *aio;
	void                *aio_arg;
	parquet_file_ranges *ranges;
	char                *topic;
};

typedef struct {
	const char *filename;
	uint64_t    keys[2];
} parquet_filename_range;

parquet_data *parquet_data_alloc(char **schema,
    parquet_data_packet ***payload_arr, uint64_t *ts, uint32_t col_len,
    uint32_t row_len);
void          parquet_data_free(parquet_data *data);

parquet_object *parquet_object_alloc(parquet_data *data, parquet_type type,
    nng_aio *aio, void *aio_arg, char *topic);
void parquet_object_free(parquet_object *elem);

parquet_file_range *parquet_file_range_alloc(
    uint32_t start_idx, uint32_t end_idx, char *filename);
void parquet_file_range_free(parquet_file_range *range);

void parquet_object_set_cb(parquet_object *obj, parquet_cb cb);
int  parquet_write_batch_async(parquet_object *elem);
// Write a batch to a temporary Parquet file, utilize it in scenarios where a
// single file is sufficient for writing, sending, and subsequent deletion.
int parquet_write_batch_tmp_async(parquet_object *elem);
int parquet_write_launcher(conf_exchange *conf);

const char  *parquet_find(const char *topic, uint64_t key);
const char **parquet_find_span(const char *topic,
    uint64_t start_key, uint64_t end_key, uint32_t *size);
uint64_t *parquet_get_key_span(const char **topicl, uint32_t size);

parquet_data_packet *parquet_find_data_packet(
    conf_parquet *conf, char *filename, uint64_t key);

parquet_data_packet **parquet_find_data_packets(
    conf_parquet *conf, char **filenames, uint64_t *keys, uint32_t len);

parquet_data_packet **parquet_find_data_span_packets(conf_parquet *conf,
    uint64_t start_key, uint64_t end_key, uint32_t *size, char *topic);


parquet_data_packet **parquet_find_data_span_packets_specify_file(
    conf_parquet *conf, parquet_filename_range *range, uint32_t *size);

parquet_filename_range **parquet_get_file_ranges(
    uint64_t start_key, uint64_t end_key, char *topic);

// If filename in range is NULL return all results, else one parquet file results.
parquet_data_ret **parquet_get_data_packets_in_range_by_column(
    parquet_filename_range *range, const char *topic, const char **schema,
    uint16_t schema_len, uint32_t *size);


#ifdef __cplusplus
}
#endif

#endif
