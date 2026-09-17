//
// pq_batch: nested LIST/STRUCT/PRIMITIVE column tree for parquet_data.
//

#include "nng/nng.h"
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/parquet.h"

#include <cstdio>
#include <cstring>
#include <string.h>

extern "C" {

static void *
pq_zalloc(size_t n)
{
	void *p = nng_alloc(n);
	if (p != NULL) {
		memset(p, 0, n);
	}
	return p;
}

static void *
pq_realloc(void *p, size_t old_n, size_t new_n)
{
	void *q = nng_alloc(new_n);
	if (q == NULL) {
		return NULL;
	}
	if (p != NULL && old_n > 0) {
		size_t copy = old_n < new_n ? old_n : new_n;
		memcpy(q, p, copy);
		nng_free(p, old_n);
	}
	if (new_n > old_n) {
		memset((uint8_t *) q + old_n, 0, new_n - old_n);
	}
	return q;
}

static pq_type *
pq_type_new(void)
{
	return (pq_type *) pq_zalloc(sizeof(pq_type));
}

static void
pq_type_move_into(pq_type *dst, pq_type *src)
{
	*dst = *src;
	nng_free(src, sizeof(*src));
}

static void
pq_type_free_contents(pq_type *t)
{
	uint32_t i;

	if (t == NULL) {
		return;
	}
	if (t->name != NULL) {
		nng_free(t->name, strlen(t->name) + 1);
		t->name = NULL;
	}
	for (i = 0; i < t->n_children; i++) {
		pq_type_free_contents(&t->children[i]);
	}
	if (t->children != NULL) {
		nng_free(t->children, sizeof(pq_type) * t->n_children);
		t->children = NULL;
	}
	t->n_children = 0;
}

pq_type *
pq_type_primitive(const char *name, pq_phys phys, pq_rep rep, pq_enc enc)
{
	pq_type *t = pq_type_new();
	if (t == NULL) {
		return NULL;
	}
	t->name       = nng_strdup(name != NULL ? name : "");
	t->kind       = PQ_PRIMITIVE;
	t->phys       = phys;
	t->repetition = rep;
	t->enc        = enc;
	if (t->name == NULL) {
		nng_free(t, sizeof(*t));
		return NULL;
	}
	return t;
}

pq_type *
pq_type_struct(const char *name, pq_rep rep, pq_type **fields, uint32_t n)
{
	uint32_t i;
	pq_type *t = pq_type_new();
	if (t == NULL) {
		return NULL;
	}
	t->name       = nng_strdup(name != NULL ? name : "schema");
	t->kind       = PQ_STRUCT;
	t->repetition = rep;
	t->n_children = n;
	if (t->name == NULL) {
		nng_free(t, sizeof(*t));
		return NULL;
	}
	if (n > 0) {
		t->children = (pq_type *) pq_zalloc(sizeof(pq_type) * n);
		if (t->children == NULL) {
			pq_type_free(t);
			return NULL;
		}
		for (i = 0; i < n; i++) {
			if (fields == NULL || fields[i] == NULL) {
				pq_type_free(t);
				return NULL;
			}
			pq_type_move_into(&t->children[i], fields[i]);
		}
	}
	return t;
}

pq_type *
pq_type_list(const char *name, pq_rep rep, pq_type *elem)
{
	pq_type *t;

	if (elem == NULL) {
		return NULL;
	}
	t = pq_type_new();
	if (t == NULL) {
		pq_type_free(elem);
		return NULL;
	}
	t->name       = nng_strdup(name != NULL ? name : "list");
	t->kind       = PQ_LIST;
	t->repetition = rep;
	t->n_children = 1;
	t->children   = (pq_type *) pq_zalloc(sizeof(pq_type));
	if (t->name == NULL || t->children == NULL) {
		pq_type_free(elem);
		pq_type_free(t);
		return NULL;
	}
	pq_type_move_into(&t->children[0], elem);
	return t;
}

void
pq_type_free(pq_type *t)
{
	if (t == NULL) {
		return;
	}
	pq_type_free_contents(t);
	nng_free(t, sizeof(*t));
}

static int
pq_array_init(pq_array *a, const pq_type *t, uint32_t length)
{
	uint32_t i;

	memset(a, 0, sizeof(*a));
	a->type   = t;
	a->length = length;
	if (t->repetition == PQ_OPTIONAL && length > 0) {
		a->valid = (uint8_t *) nng_alloc(length);
		if (a->valid == NULL) {
			return -1;
		}
		memset(a->valid, 1, length);
	}
	switch (t->kind) {
	case PQ_PRIMITIVE:
		switch (t->phys) {
		case PQ_INT32:
			if (length > 0) {
				a->i32 = (int32_t *) pq_zalloc(
				    sizeof(int32_t) * length);
				if (a->i32 == NULL) {
					return -1;
				}
			}
			break;
		case PQ_INT64:
			if (length > 0) {
				a->i64 = (int64_t *) pq_zalloc(
				    sizeof(int64_t) * length);
				if (a->i64 == NULL) {
					return -1;
				}
			}
			break;
		case PQ_BYTE_ARRAY:
			if (length > 0) {
				a->bin = (parquet_data_packet **) pq_zalloc(
				    sizeof(parquet_data_packet *) * length);
				if (a->bin == NULL) {
					return -1;
				}
			}
			break;
		}
		break;
	case PQ_LIST:
		a->offsets =
		    (uint32_t *) pq_zalloc(sizeof(uint32_t) * (length + 1));
		if (a->offsets == NULL && length + 1 > 0) {
			return -1;
		}
		if (t->n_children != 1) {
			return -1;
		}
		a->child = pq_array_from_type(&t->children[0], 0);
		if (a->child == NULL) {
			return -1;
		}
		break;
	case PQ_STRUCT:
		if (t->n_children == 0) {
			break;
		}
		a->fields =
		    (pq_array **) pq_zalloc(sizeof(pq_array *) * t->n_children);
		if (a->fields == NULL) {
			return -1;
		}
		for (i = 0; i < t->n_children; i++) {
			a->fields[i] =
			    pq_array_from_type(&t->children[i], length);
			if (a->fields[i] == NULL) {
				return -1;
			}
		}
		break;
	}
	return 0;
}

pq_array *
pq_array_from_type(const pq_type *t, uint32_t length)
{
	pq_array *a;

	if (t == NULL) {
		return NULL;
	}
	a = (pq_array *) pq_zalloc(sizeof(pq_array));
	if (a == NULL) {
		return NULL;
	}
	if (pq_array_init(a, t, length) != 0) {
		pq_array_free(a);
		return NULL;
	}
	return a;
}

static void
pq_packet_free(parquet_data_packet *pkt)
{
	if (pkt == NULL) {
		return;
	}
	if (pkt->data != NULL && pkt->size > 0) {
		nng_free(pkt->data, pkt->size);
	}
	nng_free(pkt, sizeof(*pkt));
}

void
pq_array_free(pq_array *a)
{
	uint32_t i;

	if (a == NULL) {
		return;
	}
	if (a->valid != NULL) {
		nng_free(a->valid, a->length);
		a->valid = NULL;
	}
	if (a->offsets != NULL) {
		nng_free(a->offsets, sizeof(uint32_t) * (a->length + 1));
		a->offsets = NULL;
	}
	if (!a->borrowed) {
		if (a->i32 != NULL) {
			nng_free(a->i32, sizeof(int32_t) * a->length);
		}
		if (a->i64 != NULL) {
			nng_free(a->i64, sizeof(int64_t) * a->length);
		}
		if (a->bin != NULL) {
			for (i = 0; i < a->length; i++) {
				pq_packet_free(a->bin[i]);
			}
			nng_free(
			    a->bin, sizeof(parquet_data_packet *) * a->length);
		}
	}
	a->i32 = NULL;
	a->i64 = NULL;
	a->bin = NULL;
	if (a->child != NULL) {
		pq_array_free(a->child);
		a->child = NULL;
	}
	if (a->fields != NULL && a->type != NULL) {
		for (i = 0; i < a->type->n_children; i++) {
			pq_array_free(a->fields[i]);
		}
		nng_free(a->fields, sizeof(pq_array *) * a->type->n_children);
		a->fields = NULL;
	}
	nng_free(a, sizeof(*a));
}

int
pq_array_resize(pq_array *a, uint32_t length)
{
	uint32_t i;
	uint32_t old;

	if (a == NULL || a->type == NULL || a->borrowed) {
		return -1;
	}
	old = a->length;
	if (length == old) {
		return 0;
	}
	if (a->type->repetition == PQ_OPTIONAL || a->valid != NULL) {
		uint8_t *nv = (uint8_t *) pq_realloc(
		    a->valid, old, length);
		if (length > 0 && nv == NULL) {
			return -1;
		}
		a->valid = nv;
		if (length > old && a->valid != NULL) {
			memset(a->valid + old, 1, length - old);
		}
	}
	switch (a->type->kind) {
	case PQ_PRIMITIVE:
		switch (a->type->phys) {
		case PQ_INT32:
			a->i32 = (int32_t *) pq_realloc(a->i32,
			    sizeof(int32_t) * old, sizeof(int32_t) * length);
			if (length > 0 && a->i32 == NULL) {
				return -1;
			}
			break;
		case PQ_INT64:
			a->i64 = (int64_t *) pq_realloc(a->i64,
			    sizeof(int64_t) * old, sizeof(int64_t) * length);
			if (length > 0 && a->i64 == NULL) {
				return -1;
			}
			break;
		case PQ_BYTE_ARRAY:
			a->bin = (parquet_data_packet **) pq_realloc(a->bin,
			    sizeof(parquet_data_packet *) * old,
			    sizeof(parquet_data_packet *) * length);
			if (length > 0 && a->bin == NULL) {
				return -1;
			}
			break;
		}
		break;
	case PQ_LIST: {
		uint32_t *noff = (uint32_t *) pq_realloc(a->offsets,
		    sizeof(uint32_t) * (old + 1),
		    sizeof(uint32_t) * (length + 1));
		if (noff == NULL) {
			return -1;
		}
		a->offsets = noff;
		if (length > old) {
			uint32_t fill = (old == 0) ? 0 : a->offsets[old];
			for (i = old + 1; i <= length; i++) {
				a->offsets[i] = fill;
			}
		}
		break;
	}
	case PQ_STRUCT:
		for (i = 0; i < a->type->n_children; i++) {
			if (pq_array_resize(a->fields[i], length) != 0) {
				return -1;
			}
		}
		break;
	}
	a->length = length;
	return 0;
}

static int
pq_check_prim(pq_array *a, uint32_t i, pq_phys phys)
{
	if (a == NULL || a->type == NULL || a->type->kind != PQ_PRIMITIVE ||
	    a->type->phys != phys || i >= a->length) {
		return -1;
	}
	return 0;
}

int
pq_array_set_i32(pq_array *a, uint32_t i, int32_t v)
{
	if (pq_check_prim(a, i, PQ_INT32) != 0) {
		return -1;
	}
	a->i32[i] = v;
	if (a->valid != NULL) {
		a->valid[i] = 1;
	}
	return 0;
}

int
pq_array_set_i64(pq_array *a, uint32_t i, int64_t v)
{
	if (pq_check_prim(a, i, PQ_INT64) != 0) {
		return -1;
	}
	a->i64[i] = v;
	if (a->valid != NULL) {
		a->valid[i] = 1;
	}
	return 0;
}

int
pq_array_set_bin(pq_array *a, uint32_t i, parquet_data_packet *pkt)
{
	if (pq_check_prim(a, i, PQ_BYTE_ARRAY) != 0) {
		return -1;
	}
	if (!a->borrowed && a->bin[i] != NULL && a->bin[i] != pkt) {
		pq_packet_free(a->bin[i]);
	}
	a->bin[i] = pkt;
	if (a->valid != NULL) {
		a->valid[i] = (pkt != NULL) ? 1 : 0;
	}
	return 0;
}

int
pq_array_set_null(pq_array *a, uint32_t i)
{
	if (a == NULL || i >= a->length) {
		return -1;
	}
	if (a->type != NULL && a->type->repetition == PQ_REQUIRED) {
		return -1;
	}
	if (a->valid == NULL && a->length > 0) {
		a->valid = (uint8_t *) nng_alloc(a->length);
		if (a->valid == NULL) {
			return -1;
		}
		memset(a->valid, 1, a->length);
	}
	if (a->valid != NULL) {
		a->valid[i] = 0;
	}
	return 0;
}

pq_array *
pq_struct_field(pq_array *st, const char *name)
{
	uint32_t i;

	if (st == NULL || st->type == NULL || st->type->kind != PQ_STRUCT ||
	    name == NULL || st->fields == NULL) {
		return NULL;
	}
	for (i = 0; i < st->type->n_children; i++) {
		if (st->type->children[i].name != NULL &&
		    strcmp(st->type->children[i].name, name) == 0) {
			return st->fields[i];
		}
	}
	return NULL;
}

int
pq_struct_set_null(pq_array *st, uint32_t row)
{
	return pq_array_set_null(st, row);
}

static int
pq_list_ready_row(pq_array *list, uint32_t row)
{
	if (list == NULL || list->type == NULL ||
	    list->type->kind != PQ_LIST || list->offsets == NULL ||
	    list->child == NULL || row >= list->length) {
		return -1;
	}
	if (list->offsets[row] != list->child->length) {
		return -1;
	}
	return 0;
}

int
pq_list_put_n(pq_array *list, uint32_t row, uint32_t n)
{
	uint32_t start;

	if (pq_list_ready_row(list, row) != 0) {
		return -1;
	}
	start = list->child->length;
	if (pq_array_resize(list->child, start + n) != 0) {
		return -1;
	}
	list->offsets[row + 1] = start + n;
	if (list->valid != NULL) {
		list->valid[row] = 1;
	}
	return 0;
}

int
pq_list_put_empty(pq_array *list, uint32_t row)
{
	return pq_list_put_n(list, row, 0);
}

int
pq_list_put_null(pq_array *list, uint32_t row)
{
	if (pq_list_put_empty(list, row) != 0) {
		return -1;
	}
	return pq_array_set_null(list, row);
}

int
pq_list_put_i32(pq_array *list, uint32_t row, const int32_t *v, uint32_t n)
{
	uint32_t start;

	if (pq_list_ready_row(list, row) != 0) {
		return -1;
	}
	if (list->child->type->kind != PQ_PRIMITIVE ||
	    list->child->type->phys != PQ_INT32) {
		return -1;
	}
	start = list->child->length;
	if (pq_array_resize(list->child, start + n) != 0) {
		return -1;
	}
	if (n > 0 && v != NULL) {
		memcpy(list->child->i32 + start, v, sizeof(int32_t) * n);
	}
	list->offsets[row + 1] = start + n;
	if (list->valid != NULL) {
		list->valid[row] = 1;
	}
	return 0;
}

int
pq_list_put_bin(
    pq_array *list, uint32_t row, parquet_data_packet **pkts, uint32_t n)
{
	uint32_t i;
	uint32_t start;

	if (pq_list_ready_row(list, row) != 0) {
		return -1;
	}
	if (list->child->type->kind != PQ_PRIMITIVE ||
	    list->child->type->phys != PQ_BYTE_ARRAY) {
		return -1;
	}
	start = list->child->length;
	if (pq_array_resize(list->child, start + n) != 0) {
		return -1;
	}
	for (i = 0; i < n; i++) {
		list->child->bin[start + i] = (pkts != NULL) ? pkts[i] : NULL;
	}
	list->offsets[row + 1] = start + n;
	if (list->valid != NULL) {
		list->valid[row] = 1;
	}
	return 0;
}

parquet_data_packet *
pq_packet_copy(const uint8_t *data, uint32_t size)
{
	parquet_data_packet *pkt;

	pkt = (parquet_data_packet *) pq_zalloc(sizeof(parquet_data_packet));
	if (pkt == NULL) {
		return NULL;
	}
	pkt->size = size;
	if (size > 0 && data != NULL) {
		pkt->data = (uint8_t *) nng_alloc(size);
		if (pkt->data == NULL) {
			nng_free(pkt, sizeof(*pkt));
			return NULL;
		}
		memcpy(pkt->data, data, size);
	}
	return pkt;
}

static pq_array *
pq_find_ts_leaf(pq_array *root)
{
	if (root == NULL || root->type == NULL) {
		return NULL;
	}
	if (root->type->kind == PQ_STRUCT) {
		return pq_struct_field(root, "ts");
	}
	if (root->type->kind == PQ_PRIMITIVE && root->type->phys == PQ_INT64 &&
	    root->type->name != NULL && strcmp(root->type->name, "ts") == 0) {
		return root;
	}
	return NULL;
}

void
pq_batch_bind_ts(parquet_data *batch)
{
	pq_array *ts;

	if (batch == NULL) {
		return;
	}
	ts = pq_find_ts_leaf(batch->root);
	if (ts != NULL && ts->i64 != NULL) {
		batch->ts = (uint64_t *) ts->i64;
	}
}

pq_array *
pq_batch_field(parquet_data *batch, const char *name)
{
	if (batch == NULL) {
		return NULL;
	}
	return pq_struct_field(batch->root, name);
}

bool
pq_batch_is_flat_ba(const parquet_data *batch)
{
	uint32_t       i;
	const pq_type *t;

	if (batch == NULL || batch->root == NULL || batch->schema_tree == NULL) {
		return false;
	}
	t = batch->schema_tree;
	if (t->kind != PQ_STRUCT || t->n_children < 1) {
		return false;
	}
	if (t->children[0].kind != PQ_PRIMITIVE ||
	    t->children[0].phys != PQ_INT64 ||
	    t->children[0].name == NULL ||
	    strcmp(t->children[0].name, "ts") != 0) {
		return false;
	}
	for (i = 1; i < t->n_children; i++) {
		if (t->children[i].kind != PQ_PRIMITIVE ||
		    t->children[i].phys != PQ_BYTE_ARRAY) {
			return false;
		}
	}
	return true;
}

int
pq_batch_attach_flat(parquet_data *data)
{
	uint32_t  i;
	uint32_t  n;
	pq_type **fields;
	pq_type  *root_t;
	pq_array *root;

	if (data == NULL || data->schema == NULL || data->row_len == 0 ||
	    data->col_len == 0) {
		return -1;
	}
	n      = data->col_len;
	fields = (pq_type **) pq_zalloc(sizeof(pq_type *) * n);
	if (fields == NULL) {
		return -1;
	}
	fields[0] = pq_type_primitive(
	    data->schema[0] != NULL ? data->schema[0] : "ts", PQ_INT64,
	    PQ_REQUIRED, PQ_ENC_DELTA);
	if (fields[0] == NULL) {
		nng_free(fields, sizeof(pq_type *) * n);
		return -1;
	}
	for (i = 1; i < n; i++) {
		const char *nm  = data->schema[i] != NULL ? data->schema[i] : "";
		size_t      nml = strlen(nm);
		pq_enc      enc = PQ_ENC_PLAIN;

		if ((nml >= 7 && strcmp(nm + nml - 7, ".tsdiff") == 0) ||
		    (nml >= 4 && strcmp(nm + nml - 4, ".len") == 0)) {
			enc = PQ_ENC_RLE_DICT;
		} else {
			const char *dot = strrchr(nm, '.');

			if (dot != NULL && dot[1] == 'b' && dot[2] >= '0' &&
			    dot[2] <= '9') {
				enc = PQ_ENC_RLE_DICT;
			}
		}
		fields[i] = pq_type_primitive(
		    nm, PQ_BYTE_ARRAY, PQ_OPTIONAL, enc);
		if (fields[i] == NULL) {
			uint32_t j;
			for (j = 0; j < i; j++) {
				pq_type_free(fields[j]);
			}
			nng_free(fields, sizeof(pq_type *) * n);
			return -1;
		}
	}
	root_t = pq_type_struct("schema", PQ_REQUIRED, fields, n);
	nng_free(fields, sizeof(pq_type *) * n);
	if (root_t == NULL) {
		return -1;
	}
	root = pq_array_from_type(root_t, data->row_len);
	if (root == NULL) {
		pq_type_free(root_t);
		return -1;
	}
	/* Alias legacy buffers; parquet_data_free still owns them. */
	if (root->fields != NULL && n > 0) {
		pq_array_free(root->fields[0]);
		root->fields[0]           = (pq_array *) pq_zalloc(sizeof(pq_array));
		if (root->fields[0] == NULL) {
			pq_array_free(root);
			pq_type_free(root_t);
			return -1;
		}
		root->fields[0]->type     = &root_t->children[0];
		root->fields[0]->length   = data->row_len;
		root->fields[0]->i64      = (int64_t *) data->ts;
		root->fields[0]->borrowed = 1;
		for (i = 1; i < n; i++) {
			pq_array_free(root->fields[i]);
			root->fields[i] = (pq_array *) pq_zalloc(sizeof(pq_array));
			if (root->fields[i] == NULL) {
				pq_array_free(root);
				pq_type_free(root_t);
				return -1;
			}
			root->fields[i]->type     = &root_t->children[i];
			root->fields[i]->length   = data->row_len;
			root->fields[i]->borrowed = 1;
			if (data->payload_arr != NULL) {
				root->fields[i]->bin = data->payload_arr[i - 1];
			}
		}
	}
	data->schema_tree = root_t;
	data->root        = root;
	return 0;
}

parquet_data *
pq_batch_from_type(pq_type *schema, uint32_t row_len)
{
	parquet_data *data;

	if (schema == NULL) {
		return NULL;
	}
	data = new parquet_data();
	if (data == NULL) {
		return NULL;
	}
	memset(data, 0, sizeof(*data));
	data->schema_tree = schema;
	data->row_len     = row_len;
	data->root        = pq_array_from_type(schema, row_len);
	if (data->root == NULL) {
		delete data;
		pq_type_free(schema);
		return NULL;
	}
	if (schema->kind == PQ_STRUCT) {
		data->col_len = schema->n_children;
	}
	pq_batch_bind_ts(data);
	return data;
}

parquet_data *
pq_batch_flat_alloc(char **schema, parquet_data_packet ***payload_arr,
    uint64_t *ts, uint32_t col_len, uint32_t row_len)
{
	return parquet_data_alloc(schema, payload_arr, ts, col_len, row_len);
}

static pq_type *
pq_i32_list_dict(const char *name)
{
	pq_type *elem;

	elem = pq_type_primitive(
	    "element", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	return pq_type_list(name, PQ_OPTIONAL, elem);
}

static pq_type *
pq_make_plane_group(const char *name, pq_rep rep, uint32_t n_planes)
{
	uint32_t  n_fields;
	uint32_t  i;
	pq_type **fields;
	pq_type  *st;

	if (n_planes > 64) {
		n_planes = 64;
	}
	n_fields = 2 + n_planes;
	fields   = (pq_type **) pq_zalloc(sizeof(pq_type *) * n_fields);
	if (fields == NULL) {
		return NULL;
	}
	fields[0] = pq_i32_list_dict("tsdiff");
	fields[1] = pq_i32_list_dict("len");
	for (i = 0; i < n_planes; i++) {
		char bname[8];

		snprintf(bname, sizeof(bname), "b%u", i);
		fields[2 + i] = pq_i32_list_dict(bname);
	}
	for (i = 0; i < n_fields; i++) {
		if (fields[i] == NULL) {
			uint32_t j;
			for (j = 0; j < n_fields; j++) {
				pq_type_free(fields[j]);
			}
			nng_free(fields, sizeof(pq_type *) * n_fields);
			return NULL;
		}
	}
	st = pq_type_struct(name, rep, fields, n_fields);
	nng_free(fields, sizeof(pq_type *) * n_fields);
	return st;
}

pq_type *
pq_type_schema_stream_nested(uint32_t n_planes)
{
	uint32_t  n_elem;
	uint32_t  i;
	pq_type **elem_fields;
	pq_type  *fields[2];
	pq_type  *elem;

	if (n_planes > 64) {
		n_planes = 64;
	}
	n_elem      = 4 + n_planes;
	elem_fields = (pq_type **) pq_zalloc(sizeof(pq_type *) * n_elem);
	if (elem_fields == NULL) {
		return NULL;
	}
	elem_fields[0] = pq_type_primitive(
	    "busid", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	elem_fields[1] = pq_type_primitive(
	    "canid", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	elem_fields[2] = pq_i32_list_dict("tsdiff");
	elem_fields[3] = pq_i32_list_dict("len");
	for (i = 0; i < n_planes; i++) {
		char bname[8];

		snprintf(bname, sizeof(bname), "b%u", i);
		elem_fields[4 + i] = pq_i32_list_dict(bname);
	}
	for (i = 0; i < n_elem; i++) {
		if (elem_fields[i] == NULL) {
			uint32_t j;
			for (j = 0; j < n_elem; j++) {
				pq_type_free(elem_fields[j]);
			}
			nng_free(elem_fields, sizeof(pq_type *) * n_elem);
			return NULL;
		}
	}
	elem = pq_type_struct("element", PQ_REQUIRED, elem_fields, n_elem);
	nng_free(elem_fields, sizeof(pq_type *) * n_elem);
	if (elem == NULL) {
		return NULL;
	}
	fields[0] = pq_type_primitive(
	    "ts", PQ_INT64, PQ_REQUIRED, PQ_ENC_DELTA);
	fields[1] = pq_type_list("streams", PQ_OPTIONAL, elem);
	if (fields[0] == NULL || fields[1] == NULL) {
		pq_type_free(fields[0]);
		pq_type_free(fields[1]);
		return NULL;
	}
	return pq_type_struct("schema", PQ_REQUIRED, fields, 2);
}

pq_type *
pq_type_schema_dyn_groups(const char **group_names, uint32_t n_groups,
    uint32_t n_planes)
{
	uint32_t  i;
	pq_type **fields;
	pq_type  *root;

	fields = (pq_type **) pq_zalloc(sizeof(pq_type *) * (n_groups + 1));
	if (fields == NULL) {
		return NULL;
	}
	fields[0] = pq_type_primitive(
	    "ts", PQ_INT64, PQ_REQUIRED, PQ_ENC_DELTA);
	if (fields[0] == NULL) {
		nng_free(fields, sizeof(pq_type *) * (n_groups + 1));
		return NULL;
	}
	for (i = 0; i < n_groups; i++) {
		fields[i + 1] = pq_make_plane_group(
		    group_names != NULL ? group_names[i] : "group",
		    PQ_OPTIONAL, n_planes);
		if (fields[i + 1] == NULL) {
			uint32_t j;
			for (j = 0; j <= i; j++) {
				pq_type_free(fields[j]);
			}
			nng_free(fields, sizeof(pq_type *) * (n_groups + 1));
			return NULL;
		}
	}
	root = pq_type_struct("schema", PQ_REQUIRED, fields, n_groups + 1);
	nng_free(fields, sizeof(pq_type *) * (n_groups + 1));
	return root;
}

pq_type *
pq_type_schema_can_frames(uint32_t n_planes)
{
	uint32_t  n_fields;
	uint32_t  i;
	pq_type **fields;
	pq_type  *root;

	if (n_planes > 64) {
		n_planes = 64;
	}
	n_fields = 5 + n_planes;
	fields   = (pq_type **) pq_zalloc(sizeof(pq_type *) * n_fields);
	if (fields == NULL) {
		return NULL;
	}
	fields[0] = pq_type_primitive(
	    "ts", PQ_INT64, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	fields[1] = pq_type_primitive(
	    "busid", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	fields[2] = pq_type_primitive(
	    "canid", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	fields[3] = pq_type_primitive(
	    "tsdiff", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	fields[4] = pq_type_primitive(
	    "len", PQ_INT32, PQ_REQUIRED, PQ_ENC_RLE_DICT);
	for (i = 0; i < n_planes; i++) {
		char bname[8];

		snprintf(bname, sizeof(bname), "b%u", i);
		fields[5 + i] = pq_type_primitive(
		    bname, PQ_INT32, PQ_OPTIONAL, PQ_ENC_ADAPTIVE);
	}
	for (i = 0; i < n_fields; i++) {
		if (fields[i] == NULL) {
			uint32_t j;
			for (j = 0; j < n_fields; j++) {
				pq_type_free(fields[j]);
			}
			nng_free(fields, sizeof(pq_type *) * n_fields);
			return NULL;
		}
	}
	root = pq_type_struct("schema", PQ_REQUIRED, fields, n_fields);
	nng_free(fields, sizeof(pq_type *) * n_fields);
	return root;
}

pq_enc
pq_enc_choose_i32(const pq_array *a)
{
	uint8_t  seen[256];
	uint32_t i;
	uint32_t n_present;
	uint32_t n_unique;
	uint32_t n_pairs;
	uint32_t n_small;
	int      byte_like;
	int      have_prev;
	int32_t  prev;

	if (a == NULL || a->i32 == NULL || a->length == 0) {
		return PQ_ENC_RLE_DICT;
	}
	memset(seen, 0, sizeof(seen));
	n_present = 0;
	n_unique  = 0;
	n_pairs   = 0;
	n_small   = 0;
	byte_like = 1;
	have_prev = 0;
	prev      = 0;
	for (i = 0; i < a->length; i++) {
		int32_t v;
		int32_t d;

		if (a->valid != NULL && a->valid[i] == 0) {
			continue;
		}
		v = a->i32[i];
		if (v < 0 || v > 255) {
			byte_like = 0;
		} else if (seen[v] == 0) {
			seen[v] = 1;
			n_unique++;
		}
		n_present++;
		if (have_prev) {
			n_pairs++;
			d = v - prev;
			if (d < 0) {
				d = -d;
			}
			if (byte_like && prev >= 0 && prev <= 255 && d > 128) {
				d = 256 - d;
			}
			if (d <= 2) {
				n_small++;
			}
		}
		prev      = v;
		have_prev = 1;
	}
	if (!byte_like || n_present < 2 || n_pairs == 0) {
		return PQ_ENC_RLE_DICT;
	}
	if (n_unique <= 24) {
		return PQ_ENC_RLE_DICT;
	}
	if (n_unique >= 40 && n_small * 100 >= n_pairs * 70) {
		return PQ_ENC_DELTA;
	}
	return PQ_ENC_RLE_DICT;
}

static void
pq_type_resolve_adaptive(pq_type *t, pq_array *a)
{
	uint32_t i;

	if (t == NULL || a == NULL) {
		return;
	}
	if (t->kind == PQ_PRIMITIVE) {
		if (t->enc == PQ_ENC_ADAPTIVE) {
			if (t->phys == PQ_INT32) {
				t->enc = pq_enc_choose_i32(a);
			} else {
				t->enc = PQ_ENC_DEFAULT;
			}
		}
		return;
	}
	if (t->kind == PQ_LIST) {
		if (t->n_children > 0) {
			pq_type_resolve_adaptive(&t->children[0], a->child);
		}
		return;
	}
	for (i = 0; i < t->n_children && a->fields != NULL; i++) {
		pq_type_resolve_adaptive(&t->children[i], a->fields[i]);
	}
}

void
pq_batch_resolve_adaptive(parquet_data *data)
{
	if (data == NULL || data->schema_tree == NULL || data->root == NULL) {
		return;
	}
	pq_type_resolve_adaptive(data->schema_tree, data->root);
}

} // extern "C"
