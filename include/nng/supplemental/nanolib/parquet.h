#ifndef PARQUET_H
#define PARQUET_H
#include "nng/nng.h"
#include "nng/supplemental/nanolib/conf.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct parquet_object   parquet_object;
typedef struct parquet_data     parquet_data;
typedef struct parquet_data_ret parquet_data_ret;
typedef struct parquet_payload  parquet_payload;
typedef struct pq_type          pq_type;
typedef struct pq_array         pq_array;
typedef parquet_data            pq_batch;
typedef void (*parquet_cb)(parquet_object *arg);

typedef enum {
	PQ_PRIMITIVE = 0,
	PQ_STRUCT,
	PQ_LIST, /* exactly one element child in children[0] */
} pq_kind;

typedef enum {
	PQ_INT32 = 0, /* tsdiff / len / busid / canid */
	PQ_INT64,     /* ts / block_ts */
	PQ_BYTE_ARRAY,
} pq_phys;

typedef enum {
	PQ_REQUIRED = 0,
	PQ_OPTIONAL,
	PQ_REPEATED, /* only for LIST element wrapping; public API uses PQ_LIST */
} pq_rep;

typedef enum {
	PQ_ENC_DEFAULT = 0, /* INT64→DELTA, INT32→RLE_DICT, BYTE_ARRAY→PLAIN */
	PQ_ENC_PLAIN,
	PQ_ENC_DELTA,
	PQ_ENC_RLE_DICT,
	PQ_ENC_ADAPTIVE, /* currently same as DEFAULT */
} pq_enc;

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

struct pq_type {
	char    *name;
	pq_kind  kind;
	pq_rep   repetition;
	pq_phys  phys; /* PRIMITIVE only */
	pq_enc   enc;  /* PRIMITIVE leaf only */
	pq_type *children;
	uint32_t n_children;
};

struct pq_array {
	const pq_type *type;
	uint32_t       length;
	uint8_t       *valid; /* 1=present, 0=null; REQUIRED may be NULL */
	uint8_t        borrowed; /* 1: do not free i32/i64/bin packets */

	/* LIST: child->length == offsets[length] */
	uint32_t *offsets;
	pq_array *child;

	/* STRUCT: each field length == this length */
	pq_array **fields;

	/* PRIMITIVE */
	int32_t              *i32;
	int64_t              *i64;
	parquet_data_packet **bin;
};

/*
 * parquet_data is a pq_batch. First five fields keep the historic
 * layout so existing codecs and inspect casts stay valid.
 * schema_tree/root describe the nested column tree. Flat alloc fills
 * both the legacy payload_arr fast path and a borrowed tree.
 */
struct parquet_data {
	uint32_t               col_len;
	uint32_t               row_len;
	uint64_t              *ts;
	char                 **schema;
	parquet_data_packet ***payload_arr;
	pq_type               *schema_tree;
	pq_array              *root;
};

struct parquet_data_ret {
	uint32_t               col_len;
	uint32_t               row_len;
	uint64_t              *ts;
	char                 **schema;
	parquet_data_packet ***payload_arr;
	pq_type               *schema_tree;
	pq_array              *root;
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

pq_type *pq_type_primitive(
    const char *name, pq_phys phys, pq_rep rep, pq_enc enc);
pq_type *pq_type_struct(
    const char *name, pq_rep rep, pq_type **fields, uint32_t n);
pq_type *pq_type_list(const char *name, pq_rep rep, pq_type *elem);
void     pq_type_free(pq_type *t);

pq_array *pq_array_from_type(const pq_type *t, uint32_t length);
void      pq_array_free(pq_array *a);
int       pq_array_resize(pq_array *a, uint32_t length);
int       pq_array_set_i32(pq_array *a, uint32_t i, int32_t v);
int       pq_array_set_i64(pq_array *a, uint32_t i, int64_t v);
int       pq_array_set_bin(pq_array *a, uint32_t i, parquet_data_packet *pkt);
int       pq_array_set_null(pq_array *a, uint32_t i);
pq_array *pq_struct_field(pq_array *st, const char *name);
int       pq_struct_set_null(pq_array *st, uint32_t row);
int       pq_list_put_i32(
          pq_array *list, uint32_t row, const int32_t *v, uint32_t n);
int pq_list_put_bin(
    pq_array *list, uint32_t row, parquet_data_packet **pkts, uint32_t n);
int pq_list_put_n(pq_array *list, uint32_t row, uint32_t n);
int pq_list_put_empty(pq_array *list, uint32_t row);
int pq_list_put_null(pq_array *list, uint32_t row);

parquet_data *pq_batch_from_type(pq_type *schema, uint32_t row_len);
int           pq_batch_attach_flat(parquet_data *data);
parquet_data *pq_batch_flat_alloc(char **schema,
    parquet_data_packet ***payload_arr, uint64_t *ts, uint32_t col_len,
    uint32_t row_len);
pq_array     *pq_batch_field(parquet_data *batch, const char *name);
bool          pq_batch_is_flat_ba(const parquet_data *batch);
void          pq_batch_bind_ts(parquet_data *batch);

pq_type *pq_type_schema_stream_nested(uint32_t n_planes);
pq_type *pq_type_schema_dyn_groups(const char **group_names,
    uint32_t n_groups, uint32_t n_planes);
/* One Parquet row per CAN frame: ts, busid, canid, tsdiff, len, b0..b{n-1}. */
pq_type *pq_type_schema_can_frames(uint32_t n_planes);
parquet_data_packet *pq_packet_copy(const uint8_t *data, uint32_t size);

int parquet_write_file(conf_parquet *conf, const char *filename,
    parquet_data *data, const char *topic, parquet_type write_type);
int parquet_read_file(
    conf_parquet *conf, const char *filename, parquet_data **out);

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
const char **parquet_find_span(
    const char *topic, uint64_t start_key, uint64_t end_key, uint32_t *size);
bool parquet_get_key_span(
    const char **topicl, uint32_t sz, uint64_t **key_span, uint64_t **sums);
void parquet_free_key_span(uint64_t *key_span, uint64_t *sums, uint32_t sz);

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
