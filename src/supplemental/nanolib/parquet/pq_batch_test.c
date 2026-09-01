#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/parquet.h"
#include <nuts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static parquet_data_packet ***
make_payload(uint32_t cols, uint32_t rows, const char *cell)
{
	uint32_t                 c;
	uint32_t                 r;
	size_t                   n;
	parquet_data_packet ***arr;

	arr = calloc(cols, sizeof(*arr));
	NUTS_TRUE(arr != NULL);
	n = strlen(cell);
	for (c = 0; c < cols; c++) {
		arr[c] = calloc(rows, sizeof(parquet_data_packet *));
		NUTS_TRUE(arr[c] != NULL);
		for (r = 0; r < rows; r++) {
			arr[c][r] = pq_packet_copy((const uint8_t *) cell,
			    (uint32_t) n);
			NUTS_TRUE(arr[c][r] != NULL);
		}
	}
	return arr;
}

static char **
make_names(const char **src, uint32_t n)
{
	uint32_t i;
	char   **out = calloc(n, sizeof(char *));
	NUTS_TRUE(out != NULL);
	for (i = 0; i < n; i++) {
		out[i] = nng_strdup(src[i]);
		NUTS_TRUE(out[i] != NULL);
	}
	return out;
}

static uint64_t *
make_ts(uint32_t n)
{
	uint32_t  i;
	uint64_t *ts = calloc(n, sizeof(uint64_t));
	NUTS_TRUE(ts != NULL);
	for (i = 0; i < n; i++) {
		ts[i] = 1000 + i;
	}
	return ts;
}

static void
test_flat_alloc_tree(void)
{
	const char              *src[] = { "ts", "topic", "payload" };
	char                   **schema;
	uint64_t                *ts;
	parquet_data_packet   ***payload;
	parquet_data            *data;

	schema  = make_names(src, 3);
	ts      = make_ts(2);
	payload = make_payload(2, 2, "ab");

	data = parquet_data_alloc(schema, payload, ts, 2, 2);
	NUTS_TRUE(data != NULL);
	NUTS_TRUE(data->col_len == 3);
	NUTS_TRUE(data->row_len == 2);
	NUTS_TRUE(data->payload_arr == payload);
	NUTS_TRUE(pq_batch_is_flat_ba(data));
	NUTS_TRUE(pq_batch_field(data, "ts") != NULL);
	NUTS_TRUE(pq_batch_field(data, "topic") != NULL);
	NUTS_TRUE(pq_batch_field(data, "payload") != NULL);
	NUTS_TRUE(data->root->fields[0]->borrowed == 1);
	NUTS_TRUE(data->root->fields[0]->i64 == (int64_t *) ts);

	parquet_data_free(data);
}

static conf_parquet
test_conf(void)
{
	conf_parquet conf;
	memset(&conf, 0, sizeof(conf));
	conf.enable    = true;
	conf.comp_type = UNCOMPRESSED;
	return conf;
}

static void
test_flat_write_read(void)
{
	const char              *src[] = { "ts", "data" };
	char                   **schema;
	uint64_t                *ts;
	parquet_data_packet   ***payload;
	parquet_data            *data;
	parquet_data            *got = NULL;
	conf_parquet             conf;
	char                     path[128];

	schema  = make_names(src, 2);
	ts      = make_ts(2);
	payload = make_payload(1, 2, "xy");
	data    = parquet_data_alloc(schema, payload, ts, 1, 2);
	NUTS_TRUE(data != NULL);
	NUTS_TRUE(pq_batch_is_flat_ba(data));

	snprintf(path, sizeof(path), "/tmp/pq_batch_flat_%d.parquet",
	    (int) getpid());
	unlink(path);
	conf = test_conf();
	NUTS_TRUE(parquet_write_file(&conf, path, data, "t", WRITE_RAW) == 0);
	NUTS_TRUE(parquet_read_file(&conf, path, &got) == 0);
	NUTS_TRUE(got != NULL);
	NUTS_TRUE(got->row_len == 2);
	NUTS_TRUE(got->ts != NULL);
	NUTS_TRUE(got->ts[0] == 1000);
	NUTS_TRUE(got->ts[1] == 1001);
	NUTS_TRUE(pq_batch_is_flat_ba(got));
	{
		pq_array *col = pq_batch_field(got, "data");
		NUTS_TRUE(col != NULL);
		NUTS_TRUE(col->bin[0] != NULL);
		NUTS_TRUE(col->bin[0]->size == 2);
		NUTS_TRUE(memcmp(col->bin[0]->data, "xy", 2) == 0);
	}

	parquet_data_free(data);
	parquet_data_free(got);
	unlink(path);
}

static void
test_scheme_a_roundtrip(void)
{
	pq_type      *schema;
	parquet_data *batch;
	parquet_data *got = NULL;
	pq_array     *streams;
	pq_array     *elem;
	conf_parquet  conf;
	char          path[128];
	int32_t       td0[] = { 1, 2 };
	int32_t       td1[] = { 3 };
	int32_t       b0a[] = { 0xaa, 0xbb };
	int32_t       b0b[] = { 0xcc };

	schema = pq_type_schema_stream_nested(1);
	NUTS_TRUE(schema != NULL);
	batch = pq_batch_from_type(schema, 2);
	NUTS_TRUE(batch != NULL);
	NUTS_TRUE(!pq_batch_is_flat_ba(batch));
	NUTS_TRUE(pq_array_set_i64(pq_batch_field(batch, "ts"), 0, 2000) == 0);
	NUTS_TRUE(pq_array_set_i64(pq_batch_field(batch, "ts"), 1, 2100) == 0);
	pq_batch_bind_ts(batch);

	streams = pq_batch_field(batch, "streams");
	NUTS_TRUE(streams != NULL);
	NUTS_TRUE(pq_list_put_n(streams, 0, 2) == 0);
	NUTS_TRUE(pq_list_put_n(streams, 1, 1) == 0);
	elem = streams->child;
	NUTS_TRUE(pq_array_set_i32(pq_struct_field(elem, "busid"), 0, 0) == 0);
	NUTS_TRUE(pq_array_set_i32(pq_struct_field(elem, "canid"), 0, 0x100) ==
	    0);
	NUTS_TRUE(pq_array_set_i32(pq_struct_field(elem, "busid"), 1, 0) == 0);
	NUTS_TRUE(pq_array_set_i32(pq_struct_field(elem, "canid"), 1, 0x200) ==
	    0);
	NUTS_TRUE(pq_array_set_i32(pq_struct_field(elem, "busid"), 2, 1) == 0);
	NUTS_TRUE(pq_array_set_i32(pq_struct_field(elem, "canid"), 2, 0x100) ==
	    0);

	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "tsdiff"), 0, td0, 2) ==
	    0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "tsdiff"), 1, td1, 1) ==
	    0);
	NUTS_TRUE(
	    pq_list_put_i32(pq_struct_field(elem, "tsdiff"), 2, NULL, 0) == 0);

	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "len"), 0, td0, 2) == 0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "len"), 1, td1, 1) == 0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "len"), 2, NULL, 0) == 0);

	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "b0"), 0, b0a, 2) == 0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "b0"), 1, b0b, 1) == 0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(elem, "b0"), 2, NULL, 0) == 0);

	snprintf(path, sizeof(path), "/tmp/pq_batch_a_%d.parquet",
	    (int) getpid());
	unlink(path);
	conf = test_conf();
	NUTS_ASSERT(parquet_write_file(&conf, path, batch, "can", WRITE_CAN) ==
	    0);
	NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(got->row_len == 2);
	NUTS_TRUE(got->ts[0] == 2000);
	NUTS_TRUE(!pq_batch_is_flat_ba(got));
	{
		pq_array *s  = pq_batch_field(got, "streams");
		pq_array *el = s->child;
		NUTS_TRUE(s->offsets[1] - s->offsets[0] == 2);
		NUTS_TRUE(s->offsets[2] - s->offsets[1] == 1);
		NUTS_TRUE(pq_struct_field(el, "canid")->i32[0] == 0x100);
		NUTS_TRUE(pq_struct_field(el, "canid")->i32[1] == 0x200);
		NUTS_TRUE(pq_struct_field(el, "canid")->i32[2] == 0x100);
		NUTS_TRUE(pq_struct_field(el, "tsdiff")->child->i32[0] == 1);
		NUTS_TRUE(pq_struct_field(el, "tsdiff")->child->i32[1] == 2);
		NUTS_TRUE(pq_struct_field(el, "b0")->child->i32[0] == 0xaa);
		NUTS_TRUE(pq_struct_field(el, "b0")->child->i32[1] == 0xbb);
	}

	parquet_data_free(batch);
	parquet_data_free(got);
	unlink(path);
}

static void
test_scheme_b_roundtrip(void)
{
	const char  *groups[] = { "s_0_100", "s_0_200" };
	pq_type      *schema;
	parquet_data *batch;
	parquet_data *got = NULL;
	pq_array     *g0;
	pq_array     *g1;
	conf_parquet  conf;
	char          path[128];
	int32_t       td[] = { 10, 20 };
	int32_t       b0[] = { 0x11, 0x22 };

	schema = pq_type_schema_dyn_groups(groups, 2, 1);
	NUTS_TRUE(schema != NULL);
	batch = pq_batch_from_type(schema, 2);
	NUTS_TRUE(batch != NULL);
	NUTS_TRUE(!pq_batch_is_flat_ba(batch));
	NUTS_TRUE(pq_array_set_i64(pq_batch_field(batch, "ts"), 0, 3000) == 0);
	NUTS_TRUE(pq_array_set_i64(pq_batch_field(batch, "ts"), 1, 3100) == 0);
	pq_batch_bind_ts(batch);

	g0 = pq_batch_field(batch, "s_0_100");
	g1 = pq_batch_field(batch, "s_0_200");
	NUTS_TRUE(g0 != NULL && g1 != NULL);

	NUTS_TRUE(pq_list_put_i32(pq_struct_field(g0, "tsdiff"), 0, td, 2) ==
	    0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(g0, "len"), 0, td, 2) == 0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(g0, "b0"), 0, b0, 2) == 0);
	NUTS_TRUE(pq_list_put_empty(pq_struct_field(g0, "tsdiff"), 1) == 0);
	NUTS_TRUE(pq_list_put_empty(pq_struct_field(g0, "len"), 1) == 0);
	NUTS_TRUE(pq_list_put_empty(pq_struct_field(g0, "b0"), 1) == 0);
	NUTS_TRUE(pq_struct_set_null(g0, 1) == 0);

	NUTS_TRUE(pq_list_put_empty(pq_struct_field(g1, "tsdiff"), 0) == 0);
	NUTS_TRUE(pq_list_put_empty(pq_struct_field(g1, "len"), 0) == 0);
	NUTS_TRUE(pq_list_put_empty(pq_struct_field(g1, "b0"), 0) == 0);
	NUTS_TRUE(pq_struct_set_null(g1, 0) == 0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(g1, "tsdiff"), 1, td, 1) ==
	    0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(g1, "len"), 1, td, 1) == 0);
	NUTS_TRUE(pq_list_put_i32(pq_struct_field(g1, "b0"), 1, b0, 1) == 0);

	snprintf(path, sizeof(path), "/tmp/pq_batch_b_%d.parquet",
	    (int) getpid());
	unlink(path);
	conf = test_conf();
	NUTS_ASSERT(parquet_write_file(&conf, path, batch, "can", WRITE_CAN) ==
	    0);
	NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(got->row_len == 2);
	NUTS_TRUE(got->ts[0] == 3000);
	{
		pq_array *rg0 = pq_batch_field(got, "s_0_100");
		pq_array *rg1 = pq_batch_field(got, "s_0_200");
		NUTS_TRUE(rg0 != NULL && rg1 != NULL);
		NUTS_TRUE(rg0->valid == NULL || rg0->valid[0] == 1);
		NUTS_TRUE(rg0->valid != NULL && rg0->valid[1] == 0);
		NUTS_TRUE(rg1->valid != NULL && rg1->valid[0] == 0);
		NUTS_TRUE(pq_struct_field(rg0, "tsdiff")->child->i32[0] == 10);
		NUTS_TRUE(pq_struct_field(rg0, "tsdiff")->child->i32[1] == 20);
		NUTS_TRUE(pq_struct_field(rg1, "tsdiff")->offsets[1] ==
		    pq_struct_field(rg1, "tsdiff")->offsets[0]);
	}

	parquet_data_free(batch);
	parquet_data_free(got);
	unlink(path);
}

NUTS_TESTS = {
	{ "pq_batch flat alloc tree", test_flat_alloc_tree },
	{ "pq_batch flat write/read", test_flat_write_read },
	{ "pq_batch scheme A nested", test_scheme_a_roundtrip },
	{ "pq_batch scheme B groups", test_scheme_b_roundtrip },
	{ NULL, NULL },
};
