#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/parquet.h"
#include <nuts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int pq_test_metadata_footer(const char *, const char *, const char *, const char *);
int pq_test_write_metadata_fixture(const char *, const char *, size_t, int);
int pq_test_column_statistics(const char *, const char *, int);

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
	NUTS_TRUE(got->schema_tree->metadata == NULL);
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
test_type_metadata(void)
{
	pq_type *schema = pq_type_struct("schema", PQ_REQUIRED, NULL, 0);
	char original[] = "someip:1:2:original";
	NUTS_ASSERT(schema != NULL);
	NUTS_TRUE(pq_type_set_metadata(NULL, original) == -1);
	NUTS_ASSERT(pq_type_set_metadata(schema, original) == 0);
	original[0] = 'X';
	NUTS_TRUE(strcmp(schema->metadata, "someip:1:2:original") == 0);
	/* Replacing from the existing allocation must not read freed memory. */
	NUTS_ASSERT(pq_type_set_metadata(schema, schema->metadata + 7) == 0);
	NUTS_TRUE(strcmp(schema->metadata, "1:2:original") == 0);
	NUTS_ASSERT(pq_type_set_metadata(schema, "") == 0);
	NUTS_TRUE(schema->metadata != NULL && schema->metadata[0] == '\0');
	NUTS_ASSERT(pq_type_set_metadata(schema, NULL) == 0);
	NUTS_TRUE(schema->metadata == NULL);
	NUTS_ASSERT(pq_type_set_metadata(schema, "owned by schema") == 0);
	parquet_data *batch = pq_batch_from_type(schema, 1);
	NUTS_ASSERT(batch != NULL);
	NUTS_TRUE(strcmp(batch->schema_tree->metadata, "owned by schema") == 0);
	parquet_data_free(batch);
}

static void
test_metadata_roundtrip_case(bool typed, bool encrypted)
{
	const char *manifest = "someip:1:2:0123456789abcdef";
	const char *topic = "vehicle/someip";
	parquet_data *batch;
	parquet_data *got = NULL;
	conf_parquet conf = test_conf();
	char path[128];
	char key[] = "0123456789abcdef";
	char key_id[] = "metadata-test";
	char unavailable_wrapped_key[] = "unavailable-wrapped-key";
	if (typed) {
		pq_type *fields[] = {
			pq_type_primitive("ts", PQ_INT64, PQ_REQUIRED, PQ_ENC_DELTA),
			pq_type_primitive("session", PQ_INT32, PQ_REQUIRED, PQ_ENC_DELTA)
		};
		NUTS_ASSERT(fields[0] != NULL && fields[1] != NULL);
		pq_type *schema = pq_type_struct("schema", PQ_REQUIRED, fields, 2);
		NUTS_ASSERT(schema != NULL);
		batch = pq_batch_from_type(schema, 2);
		NUTS_ASSERT(batch != NULL);
		for (uint32_t i = 0; i < 2; i++) {
			NUTS_ASSERT(pq_array_set_i64(pq_batch_field(batch, "ts"), i, 1000 + i) == 0);
			NUTS_ASSERT(pq_array_set_i32(pq_batch_field(batch, "session"), i, 7 + i) == 0);
		}
	} else {
		const char *names[] = { "ts", "payload" };
		batch = parquet_data_alloc(make_names(names, 2),
		    make_payload(1, 2, "data"), make_ts(2), 1, 2);
		NUTS_ASSERT(batch != NULL);
	}
	NUTS_ASSERT(pq_type_set_metadata(batch->schema_tree, manifest) == 0);
	if (encrypted) {
		conf.encryption.enable = true;
		conf.encryption.type = AES_GCM_V1;
		conf.encryption.plaintext_footer = true;
		conf.encryption.key = key;
		conf.encryption.key_id = key_id;
		/* Exercise the existing local-key fallback when unwrap is unavailable. */
		conf.encryption.key_cipher = unavailable_wrapped_key;
	}
	snprintf(path, sizeof(path), "/tmp/pq_metadata_%d_%d_%d.parquet",
	    (int) getpid(), typed, encrypted);
	unlink(path);
	NUTS_ASSERT(parquet_write_file(&conf, path, batch, topic, WRITE_RAW) == 0);
	parquet_data_free(batch);
	NUTS_TRUE(pq_test_metadata_footer(path, manifest, topic,
	    encrypted ? "AES_GCM_V1" : "NONE") == 0);
	NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(got->row_len == 2 && got->ts[0] == 1000 && got->ts[1] == 1001);
	NUTS_ASSERT(got->schema_tree->metadata != NULL);
	NUTS_TRUE(strcmp(got->schema_tree->metadata, manifest) == 0);
	if (typed) {
		pq_array *session = pq_batch_field(got, "session");
		NUTS_ASSERT(session != NULL);
		NUTS_TRUE(session->i32[0] == 7 && session->i32[1] == 8);
	} else {
		pq_array *payload = pq_batch_field(got, "payload");
		NUTS_ASSERT(payload != NULL);
		NUTS_TRUE(payload->bin[0]->size == 4 &&
		    memcmp(payload->bin[0]->data, "data", 4) == 0);
	}
	parquet_data_free(got);
	unlink(path);
}

static void
test_metadata_roundtrip(void)
{
	test_metadata_roundtrip_case(false, false);
	test_metadata_roundtrip_case(true, false);
}

static void
test_metadata_encrypted_roundtrip(void)
{
	test_metadata_roundtrip_case(false, true);
	test_metadata_roundtrip_case(true, true);
}

static void
test_metadata_read_guards(void)
{
	char path[128];
	const char embedded_nul[] = "valid\0hidden";
	parquet_data *got = NULL;
	conf_parquet conf = test_conf();
	snprintf(path, sizeof(path), "/tmp/pq_metadata_guards_%d.parquet", (int) getpid());
	unlink(path);
	NUTS_ASSERT(pq_test_write_metadata_fixture(path, NULL, 0, 0) == 0);
	NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(got->schema_tree->metadata == NULL && got->ts[0] == 42);
	parquet_data_free(got);
	got = NULL;
	/* Presence of an empty manifest remains distinct from legacy absence. */
	NUTS_ASSERT(pq_test_write_metadata_fixture(path, "", 0, 0) == 0);
	NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
	NUTS_ASSERT(got != NULL && got->schema_tree->metadata != NULL);
	NUTS_TRUE(got->schema_tree->metadata[0] == '\0');
	parquet_data_free(got);
	got = NULL;
	/* Even identical duplicate keys are ambiguous and must be rejected. */
	NUTS_ASSERT(pq_test_write_metadata_fixture(path, "same", 4, 1) == 0);
	NUTS_TRUE(parquet_read_file(&conf, path, &got) == -1 && got == NULL);
	NUTS_ASSERT(pq_test_write_metadata_fixture(path, embedded_nul,
	    sizeof(embedded_nul) - 1, 0) == 0);
	NUTS_TRUE(parquet_read_file(&conf, path, &got) == -1 && got == NULL);
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
		NUTS_TRUE(pq_valid_present(rg0->valid, 0));
		NUTS_TRUE(rg0->valid != NULL &&
		    !pq_valid_present(rg0->valid, 1));
		NUTS_TRUE(rg1->valid != NULL &&
		    !pq_valid_present(rg1->valid, 0));
		NUTS_TRUE(pq_struct_field(rg0, "tsdiff")->child->i32[0] == 10);
		NUTS_TRUE(pq_struct_field(rg0, "tsdiff")->child->i32[1] == 20);
		NUTS_TRUE(pq_struct_field(rg1, "tsdiff")->offsets[1] ==
		    pq_struct_field(rg1, "tsdiff")->offsets[0]);
	}

	parquet_data_free(batch);
	parquet_data_free(got);
	unlink(path);
}

static void
test_can_frames_enc_and_row_groups(void)
{
	pq_type      *schema;
	parquet_data *batch;
	parquet_data *got = NULL;
	pq_array     *b0;
	pq_array     *b1;
	conf_parquet  conf;
	char          path[128];
	uint32_t      n;
	uint32_t      i;

	schema = pq_type_schema_can_frames(2);
	NUTS_TRUE(schema != NULL);
	NUTS_TRUE(schema->children[0].enc == PQ_ENC_RLE_DICT);
	NUTS_TRUE(schema->children[5].enc == PQ_ENC_ADAPTIVE);
	NUTS_TRUE(schema->children[6].enc == PQ_ENC_ADAPTIVE);

	n     = 256;
	batch = pq_batch_from_type(schema, n);
	NUTS_TRUE(batch != NULL);
	schema = NULL;
	for (i = 0; i < n; i++) {
		NUTS_TRUE(pq_array_set_i64(pq_batch_field(batch, "ts"), i,
		              (int64_t) (1000 + (i / 40))) == 0);
		NUTS_TRUE(pq_array_set_i32(pq_batch_field(batch, "busid"), i,
		              0) == 0);
		NUTS_TRUE(pq_array_set_i32(pq_batch_field(batch, "canid"), i,
		              (int32_t) (0x100 + (i / 10000))) == 0);
		NUTS_TRUE(pq_array_set_i32(pq_batch_field(batch, "tsdiff"), i,
		              (int32_t) (i % 100)) == 0);
		NUTS_TRUE(
		    pq_array_set_i32(pq_batch_field(batch, "len"), i, 2) == 0);
		NUTS_TRUE(pq_array_set_i32(pq_batch_field(batch, "b0"), i,
		              (int32_t) (i % 256)) == 0);
		if (i == 1) {
			NUTS_TRUE(pq_array_set_null(
			              pq_batch_field(batch, "b1"), i) == 0);
		} else {
			NUTS_TRUE(pq_array_set_i32(pq_batch_field(batch, "b1"),
			              i, 0xA1) == 0);
		}
	}
	pq_batch_bind_ts(batch);
	pq_batch_resolve_adaptive(batch);
	NUTS_TRUE(batch->schema_tree->children[5].enc == PQ_ENC_DELTA);
	NUTS_TRUE(batch->schema_tree->children[6].enc == PQ_ENC_RLE_DICT);

	snprintf(path, sizeof(path), "/tmp/pq_batch_frames_%d.parquet",
	    (int) getpid());
	unlink(path);
	conf = test_conf();
	conf.dictionary = true;
	NUTS_ASSERT(
	    parquet_write_file(&conf, path, batch, "can", WRITE_CAN) == 0);
	NUTS_TRUE(parquet_file_num_row_groups(path) == 1);
	NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
	NUTS_TRUE(got != NULL);
	NUTS_TRUE(got->row_len == n);
	NUTS_TRUE(got->ts[0] == 1000);
	NUTS_TRUE(got->ts[n - 1] == (uint64_t) (1000 + ((n - 1) / 40)));
	b0 = pq_batch_field(got, "b0");
	b1 = pq_batch_field(got, "b1");
	NUTS_TRUE(b0 != NULL && b1 != NULL);
	NUTS_TRUE(b0->i32[0] == 0);
	NUTS_TRUE(b0->i32[255] == 255);
	NUTS_TRUE(b1->i32[0] == 0xA1);
	NUTS_TRUE(b1->valid != NULL && !pq_valid_present(b1->valid, 1));
	NUTS_TRUE(b1->i32[n - 1] == 0xA1);

	parquet_data_free(batch);
	parquet_data_free(got);
	unlink(path);
}

static void
test_enc_choose_i32(void)
{
	pq_type  *t;
	pq_array *a;
	uint32_t  i;

	t = pq_type_primitive("b0", PQ_INT32, PQ_OPTIONAL, PQ_ENC_ADAPTIVE);
	NUTS_TRUE(t != NULL);
	a = pq_array_from_type(t, 80);
	NUTS_TRUE(a != NULL);
	for (i = 0; i < 80; i++) {
		NUTS_TRUE(pq_array_set_i32(a, i, 0xA1) == 0);
	}
	NUTS_TRUE(pq_enc_choose_i32(a) == PQ_ENC_RLE_DICT);
	pq_array_free(a);

	/* Small nearly-injective smooth sequence → DELTA. */
	a = pq_array_from_type(t, 256);
	NUTS_TRUE(a != NULL);
	for (i = 0; i < 256; i++) {
		NUTS_TRUE(pq_array_set_i32(a, i, (int32_t) i) == 0);
	}
	NUTS_TRUE(pq_enc_choose_i32(a) == PQ_ENC_DELTA);
	pq_array_free(a);

	/* Long repeating counter plane (n ≫ unique): prefer DICT. */
	a = pq_array_from_type(t, 2000);
	NUTS_TRUE(a != NULL);
	for (i = 0; i < 2000; i++) {
		NUTS_TRUE(pq_array_set_i32(a, i, (int32_t) (i % 80)) == 0);
	}
	NUTS_TRUE(pq_enc_choose_i32(a) == PQ_ENC_RLE_DICT);
	pq_array_free(a);
	pq_type_free(t);
}

static void
test_enc_choose_u8_and_compact_schema(void)
{
	pq_type      *t;
	pq_array     *a;
	parquet_data *batch;
	pq_array     *td;
	uint32_t      i;

	t = pq_type_primitive("tsdiff", PQ_UINT8, PQ_REQUIRED, PQ_ENC_ADAPTIVE);
	NUTS_TRUE(t != NULL);
	a = pq_array_from_type(t, 80);
	NUTS_TRUE(a != NULL);
	for (i = 0; i < 80; i++) {
		NUTS_TRUE(pq_array_set_u8(a, i, 7) == 0);
	}
	NUTS_TRUE(pq_enc_choose_u8(a) == PQ_ENC_RLE_DICT);
	pq_array_free(a);

	a = pq_array_from_type(t, 256);
	NUTS_TRUE(a != NULL);
	for (i = 0; i < 256; i++) {
		NUTS_TRUE(pq_array_set_u8(a, i, (uint8_t) i) == 0);
	}
	NUTS_TRUE(pq_enc_choose_u8(a) == PQ_ENC_DELTA);
	pq_array_free(a);

	a = pq_array_from_type(t, 2000);
	NUTS_TRUE(a != NULL);
	for (i = 0; i < 2000; i++) {
		NUTS_TRUE(pq_array_set_u8(a, i, (uint8_t) (i % 80)) == 0);
	}
	NUTS_TRUE(pq_enc_choose_u8(a) == PQ_ENC_RLE_DICT);
	pq_array_free(a);
	pq_type_free(t);

	t = pq_type_schema_can_frames_compact(2);
	NUTS_TRUE(t != NULL);
	batch = pq_batch_from_type(t, 3);
	NUTS_TRUE(batch != NULL);
	NUTS_TRUE(pq_batch_field(batch, "tsdiff") == NULL);
	td = pq_batch_field(batch, "len");
	NUTS_TRUE(td != NULL && td->u8 != NULL && td->i32 == NULL);
	NUTS_TRUE(pq_array_set_u8(td, 0, 10) == 0);
	NUTS_TRUE(td->u8[0] == 10);
	NUTS_TRUE(pq_batch_field(batch, "b0")->u8 != NULL);
	parquet_data_free(batch);
}

static void
test_binary_storage_guards(void)
{
	pq_type *type = pq_type_primitive(
	    "payload", PQ_BYTE_ARRAY, PQ_OPTIONAL, PQ_ENC_RLE_DICT);
	NUTS_ASSERT(type != NULL);
	pq_array *a = pq_array_from_type(type, 2);
	NUTS_ASSERT(a != NULL);
	NUTS_TRUE(pq_array_alloc_binary(NULL, 0) == NULL);
	NUTS_TRUE(pq_array_alloc_binary(a, UINT64_MAX) == NULL);
	NUTS_TRUE(a->bin_storage == NULL && a->bin[0] == NULL);
	a->borrowed = 1;
	NUTS_TRUE(pq_array_alloc_binary(a, 0) == NULL);
	a->borrowed = 0;
	NUTS_ASSERT(pq_array_set_bin(a, 0,
	    pq_packet_copy((const uint8_t *) "kept", 4)) == 0);
	NUTS_TRUE(pq_array_alloc_binary(a, 4) == NULL);
	NUTS_TRUE(a->bin[0]->size == 4);
	NUTS_TRUE(memcmp(a->bin[0]->data, "kept", 4) == 0);
	pq_array_free(a);

	a = pq_array_from_type(type, 2);
	NUTS_ASSERT(a != NULL);
	/* Zero bytes still owns descriptors for valid empty values. */
	NUTS_ASSERT(pq_array_alloc_binary(a, 0) != NULL);
	NUTS_TRUE(a->bin[0] != NULL && a->bin[0]->size == 0);
	NUTS_TRUE(a->bin[1] != NULL && a->bin[1]->data == NULL);
	NUTS_TRUE(pq_array_alloc_binary(a, 0) == NULL);
	NUTS_TRUE(pq_array_resize(a, 3) == -1);
	NUTS_TRUE(a->length == 2);
	NUTS_TRUE(pq_array_resize(a, 2) == 0);
	NUTS_TRUE(pq_array_set_bin(a, 0, NULL) == -1);
	NUTS_TRUE(pq_array_set_null(a, 0) == 0);
	pq_array_free(a);
	a = pq_array_from_type(type, 0);
	NUTS_ASSERT(a != NULL);
	NUTS_TRUE(pq_array_alloc_binary(a, 0) == NULL);
	pq_array_free(a);
	pq_type_free(type);

	type = pq_type_primitive("int", PQ_INT32, PQ_REQUIRED, PQ_ENC_PLAIN);
	NUTS_ASSERT(type != NULL);
	a = pq_array_from_type(type, 2);
	NUTS_ASSERT(a != NULL);
	NUTS_TRUE(pq_array_alloc_binary(a, 2) == NULL);
	pq_array_free(a);
	pq_type_free(type);
}

static void
test_binary_storage_roundtrip(void)
{
	pq_type *fields[] = {
		pq_type_primitive("ts", PQ_INT64, PQ_REQUIRED, PQ_ENC_DELTA),
		pq_type_primitive("id", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT),
		pq_type_primitive("payload", PQ_BYTE_ARRAY, PQ_OPTIONAL, PQ_ENC_RLE_DICT)
	};
	for (uint32_t i = 0; i < 3; i++) {
		NUTS_ASSERT(fields[i] != NULL);
	}
	pq_type *schema = pq_type_struct("schema", PQ_REQUIRED, fields, 3);
	NUTS_ASSERT(schema != NULL);
	parquet_data *batch = pq_batch_from_type(schema, 4), *got = NULL;
	NUTS_ASSERT(batch != NULL);
	pq_array *a = pq_batch_field(batch, "payload");
	uint8_t *storage = pq_array_alloc_binary(a, 6);
	NUTS_ASSERT(storage != NULL);
	memcpy(storage, "abcdef", 6);
	a->bin[0]->data = storage;
	a->bin[0]->size = 3;
	NUTS_TRUE(pq_array_set_null(a, 1) == 0);
	/* Row 2 is present but empty; row 1 is NULL. */
	a->bin[3]->data = storage + 3;
	a->bin[3]->size = 3;
	for (uint32_t i = 0; i < 4; i++) {
		NUTS_TRUE(pq_array_set_i64(pq_batch_field(batch, "ts"), i, 100 + i) == 0);
		NUTS_TRUE(pq_array_set_i32(pq_batch_field(batch, "id"), i, 7) == 0);
	}
	char path[128];
	snprintf(path, sizeof(path), "/tmp/pq_batch_binary_%d.parquet", (int) getpid());
	conf_parquet conf = test_conf();
	conf.comp_type = ZSTD;
	char bad_path[160];
	snprintf(bad_path, sizeof(bad_path), "%s/missing.parquet", path);
	NUTS_TRUE(parquet_write_file(&conf, bad_path, batch, "t", WRITE_RAW) == -1);
	NUTS_TRUE(a->bin[0]->size == 3 && memcmp(a->bin[0]->data, "abc", 3) == 0);
	NUTS_ASSERT(parquet_write_file(&conf, path, batch, "t", WRITE_RAW) == 0);
	NUTS_TRUE(batch->root->fields[2] == NULL);
	parquet_data_free(batch); /* The writer already released the slab. */
	NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
	NUTS_ASSERT(got != NULL);
	a = pq_batch_field(got, "payload");
	NUTS_ASSERT(a != NULL);
	NUTS_TRUE(pq_valid_present(a->valid, 0));
	NUTS_TRUE(a->bin[0]->size == 3 && memcmp(a->bin[0]->data, "abc", 3) == 0);
	NUTS_TRUE(!pq_valid_present(a->valid, 1));
	NUTS_TRUE(pq_valid_present(a->valid, 2));
	NUTS_ASSERT(a->bin[2] != NULL);
	NUTS_TRUE(a->bin[2]->size == 0);
	NUTS_TRUE(a->bin[3]->size == 3 && memcmp(a->bin[3]->data, "def", 3) == 0);
	parquet_data_free(got);
	unlink(path);
}

static parquet_data *
binary_writer_batch(uint32_t rows, pq_enc encoding, bool disable_statistics)
{
	pq_type *fields[] = {
		pq_type_primitive("ts", PQ_INT64, PQ_REQUIRED, PQ_ENC_DELTA),
		pq_type_primitive("id", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT),
		pq_type_primitive("payload", PQ_BYTE_ARRAY, PQ_OPTIONAL, encoding)
	};
	for (uint32_t i = 0; i < 3; i++) {
		NUTS_ASSERT(fields[i] != NULL);
	}
	fields[2]->disable_statistics = disable_statistics;
	pq_type *schema = pq_type_struct("schema", PQ_REQUIRED, fields, 3);
	NUTS_ASSERT(schema != NULL);
	parquet_data *batch = pq_batch_from_type(schema, rows);
	NUTS_ASSERT(batch != NULL);
	/* An integer leaf keeps these tests on the typed writer path. */
	NUTS_TRUE(!pq_batch_is_flat_ba(batch));
	for (uint32_t i = 0; i < rows; i++) {
		NUTS_ASSERT(pq_array_set_i64(pq_batch_field(batch, "ts"), i, 100 + i) == 0);
		NUTS_ASSERT(pq_array_set_i32(pq_batch_field(batch, "id"), i, 7 + i) == 0);
	}
	return batch;
}

static void
test_binary_writer_layouts(void)
{
	enum { CONTIGUOUS, REORDERED, GAPPED, PACKETS, BORROWED, ALL_NULL, ALL_EMPTY };
	static const uint8_t binary[] = { 0, 255, 42 };
	static const uint8_t *values[] = {
		(const uint8_t *) "alpha", NULL, NULL, binary, NULL, (const uint8_t *) "z"
	};
	static const uint32_t sizes[] = { 5, 0, 0, 3, 0, 1 };
	for (uint32_t mode = CONTIGUOUS; mode <= ALL_EMPTY; mode++) {
		parquet_data *batch = binary_writer_batch(6, PQ_ENC_RLE_DICT, false);
		pq_array *a = pq_batch_field(batch, "payload");
		const uint8_t *expected[6];
		uint32_t lengths[6];
		bool present[6];
		parquet_data_packet borrowed[6] = { 0 };
		parquet_data_packet **borrowed_ptrs = NULL;
		for (uint32_t i = 0; i < 6; i++) {
			expected[i] = values[i];
			lengths[i] = sizes[i];
			present[i] = i != 1;
		}
		if (mode == PACKETS || mode == BORROWED) {
			if (mode == BORROWED) {
				a->borrowed = 1;
				borrowed_ptrs = a->bin;
			}
			for (uint32_t i = 0; i < 6; i++) {
				if (!present[i]) {
					NUTS_ASSERT(pq_array_set_null(a, i) == 0);
					continue;
				}
				if (mode == BORROWED) {
					borrowed[i].data = (uint8_t *) values[i];
					borrowed[i].size = sizes[i];
					a->bin[i] = &borrowed[i];
				} else {
					a->bin[i] = pq_packet_copy(values[i], sizes[i]);
					NUTS_ASSERT(a->bin[i] != NULL);
				}
			}
		} else {
			uint64_t bytes = mode == GAPPED ? 12 : 9;
			if (mode == ALL_NULL || mode == ALL_EMPTY) {
				bytes = 0;
			}
			uint8_t *storage = pq_array_alloc_binary(a, bytes);
			NUTS_ASSERT(storage != NULL);
			if (mode == ALL_NULL || mode == ALL_EMPTY) {
				for (uint32_t i = 0; i < 6; i++) {
					present[i] = mode == ALL_EMPTY;
					lengths[i] = 0;
					if (!present[i]) {
						NUTS_ASSERT(pq_array_set_null(a, i) == 0);
					}
				}
			} else {
				uint32_t binary_offset = mode == GAPPED ? 7 : 5;
				uint32_t last_offset = mode == GAPPED ? 11 : 8;
				memcpy(storage, values[0], 5);
				memcpy(storage + binary_offset, binary, 3);
				storage[last_offset] = 'z';
				a->bin[0]->data = storage;
				a->bin[0]->size = 5;
				a->bin[3]->data = storage + binary_offset;
				a->bin[3]->size = 3;
				a->bin[5]->data = storage + last_offset;
				a->bin[5]->size = 1;
				/* Both NULL data and a valid pointer represent an empty value. */
				a->bin[4]->data = storage + last_offset;
				NUTS_ASSERT(pq_array_set_null(a, 1) == 0);
				/* Null rows must not inspect unused packet data. */
				a->bin[1]->data = (uint8_t *) (uintptr_t) 1;
				a->bin[1]->size = UINT32_MAX;
				if (mode == REORDERED) {
					parquet_data_packet *first = a->bin[0];
					a->bin[0] = a->bin[3];
					a->bin[3] = first;
					expected[0] = binary;
					lengths[0] = 3;
					expected[3] = values[0];
					lengths[3] = 5;
				}
			}
		}
		char path[128];
		snprintf(path, sizeof(path), "/tmp/pq_binary_layout_%d_%u.parquet",
		    (int) getpid(), mode);
		conf_parquet conf = test_conf();
		conf.comp_type = ZSTD;
		NUTS_ASSERT(parquet_write_file(&conf, path, batch, "t", WRITE_RAW) == 0);
		NUTS_TRUE(batch->root->fields[2] == NULL);
		parquet_data_free(batch);
		if (borrowed_ptrs != NULL) {
			NUTS_TRUE(borrowed[0].data == values[0] && borrowed[0].size == 5);
			nng_free(borrowed_ptrs, 6 * sizeof(*borrowed_ptrs));
		}
		parquet_data *got = NULL;
		NUTS_ASSERT(parquet_read_file(&conf, path, &got) == 0);
		NUTS_ASSERT(got != NULL && got->row_len == 6);
		a = pq_batch_field(got, "payload");
		for (uint32_t i = 0; i < 6; i++) {
			NUTS_TRUE(got->ts[i] == 100 + i);
			NUTS_TRUE(pq_batch_field(got, "id")->i32[i] == (int32_t) (7 + i));
			NUTS_TRUE(pq_valid_present(a->valid, i) == present[i]);
			if (present[i]) {
				NUTS_ASSERT(a->bin[i] != NULL);
				NUTS_TRUE(a->bin[i]->size == lengths[i]);
				if (lengths[i] > 0) {
					NUTS_TRUE(memcmp(a->bin[i]->data, expected[i], lengths[i]) == 0);
				}
			}
		}
		parquet_data_free(got);
		unlink(path);
	}
}

static void
test_binary_writer_invalid_slab(void)
{
	for (uint32_t mode = 0; mode < 8; mode++) {
		parquet_data *batch = binary_writer_batch(2, PQ_ENC_PLAIN, false);
		pq_array *a = pq_batch_field(batch, "payload");
		uint8_t *storage = pq_array_alloc_binary(a, 4);
		NUTS_ASSERT(storage != NULL);
		memcpy(storage, "abcd", 4);
		a->bin[0]->data = storage;
		a->bin[0]->size = 4;
		size_t storage_size = a->bin_storage_size;
		switch (mode) {
		case 0: /* One byte past the payload end. */
			a->bin[0]->data = storage + 3;
			a->bin[0]->size = 2;
			break;
		case 1: /* Packet descriptors are not payload. */
			a->bin[0]->data = a->bin_storage;
			a->bin[0]->size = 1;
			break;
		case 2: /* Reject impossible offsets without allocating gigabytes. */
			a->bin[0]->size = UINT32_MAX;
			break;
		case 3:
			a->bin[0]->data = NULL;
			a->bin[0]->size = 1;
			break;
		case 4: /* Owned slabs cannot claim unrelated external storage. */
			a->bin[0]->data = (uint8_t *) "external";
			a->bin[0]->size = 8;
			break;
		case 5: /* Do not dereference an unaligned, invalid descriptor. */
			a->bin[0] = (parquet_data_packet *) ((uint8_t *) a->bin_storage + 1);
			break;
		case 6:
			a->bin_storage_size = 2 * sizeof(parquet_data_packet) - 1;
			break;
		case 7:
			a->length = 1;
			break;
		}
		char path[128];
		snprintf(path, sizeof(path), "/tmp/pq_binary_invalid_%d_%u.parquet",
		    (int) getpid(), mode);
		conf_parquet conf = test_conf();
		NUTS_TRUE(parquet_write_file(&conf, path, batch, "t", WRITE_RAW) == -1);
		/* Invalid writes may consume earlier columns; the bad slab stays owned. */
		a->length = 2;
		a->bin_storage_size = storage_size;
		parquet_data_free(batch);
		unlink(path);
	}
}

static void
test_leaf_statistics(void)
{
	for (uint32_t global = 0; global < 2; global++) {
		for (uint32_t disabled = 0; disabled < 2; disabled++) {
			for (uint32_t dictionary = 0; dictionary < 2; dictionary++) {
				parquet_data *batch = binary_writer_batch(2,
				    dictionary ? PQ_ENC_RLE_DICT : PQ_ENC_PLAIN, disabled != 0);
				pq_array *a = pq_batch_field(batch, "payload");
				a->bin[0] = pq_packet_copy((const uint8_t *) "abc", 3);
				a->bin[1] = pq_packet_copy((const uint8_t *) "xyz", 3);
				NUTS_ASSERT(a->bin[0] != NULL && a->bin[1] != NULL);
				conf_parquet conf = test_conf();
				conf.enable_statistics = global != 0;
				char path[128];
				snprintf(path, sizeof(path), "/tmp/pq_leaf_stats_%d_%u_%u_%u.parquet",
				    (int) getpid(), global, disabled, dictionary);
				NUTS_ASSERT(parquet_write_file(&conf, path, batch, "t", WRITE_RAW) == 0);
				parquet_data_free(batch);
				NUTS_TRUE(pq_test_column_statistics(path, "ts", global) == 0);
				NUTS_TRUE(pq_test_column_statistics(path, "id", global) == 0);
				NUTS_TRUE(pq_test_column_statistics(path, "payload", global && !disabled) == 0);
				unlink(path);
			}
		}
	}
}

static void
test_binary_writer_offset_overflow(void)
{
	enum { ROWS = 32768, SLICE_SIZE = 65536 };
	parquet_data *batch = binary_writer_batch(ROWS, PQ_ENC_PLAIN, false);
	pq_array *a = pq_batch_field(batch, "payload");
	uint8_t *storage = pq_array_alloc_binary(a, SLICE_SIZE);
	NUTS_ASSERT(storage != NULL);
	memset(storage, 'x', SLICE_SIZE);
	/* Every slice is individually valid, but repeated slices need 2^31
	 * logical bytes. Reject before a fallback builder allocates that much. */
	for (uint32_t i = 0; i < ROWS; i++) {
		a->bin[i]->data = storage;
		a->bin[i]->size = SLICE_SIZE;
	}
	char path[128];
	snprintf(path, sizeof(path), "/tmp/pq_binary_offset_overflow_%d.parquet",
	    (int) getpid());
	conf_parquet conf = test_conf();
	NUTS_TRUE(parquet_write_file(&conf, path, batch, "t", WRITE_RAW) == -1);
	NUTS_TRUE(batch->root->fields[2] == a);
	parquet_data_free(batch);
	unlink(path);
}

NUTS_TESTS = {
	{ "pq_type metadata ownership", test_type_metadata },
	{ "pq_batch metadata roundtrip", test_metadata_roundtrip },
	{ "pq_batch metadata encrypted roundtrip", test_metadata_encrypted_roundtrip },
	{ "pq_batch metadata read guards", test_metadata_read_guards },
	{ "pq_batch flat alloc tree", test_flat_alloc_tree },
	{ "pq_batch flat write/read", test_flat_write_read },
	{ "pq_batch scheme A nested", test_scheme_a_roundtrip },
	{ "pq_batch scheme B groups", test_scheme_b_roundtrip },
	{ "pq_enc choose i32", test_enc_choose_i32 },
	{ "pq_enc choose u8 compact", test_enc_choose_u8_and_compact_schema },
	{ "pq_batch can frames enc", test_can_frames_enc_and_row_groups },
	{ "pq_batch binary storage guards", test_binary_storage_guards },
	{ "pq_batch binary storage roundtrip", test_binary_storage_roundtrip },
	{ "pq_batch binary writer layouts", test_binary_writer_layouts },
	{ "pq_batch binary writer invalid slab", test_binary_writer_invalid_slab },
	{ "pq_batch binary writer offset overflow", test_binary_writer_offset_overflow },
	{ "pq_batch leaf statistics", test_leaf_statistics },
	{ NULL, NULL },
};
