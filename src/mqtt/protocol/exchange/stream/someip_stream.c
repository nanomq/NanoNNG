// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT

#include "someip_batch.h"
#include <openssl/evp.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SOMEIP_MANIFEST_SIZE 84u

static uint32_t
someip_read_be(const uint8_t *p, uint32_t width)
{
	uint32_t value = 0;
	for (uint32_t i = 0; i < width; i++) value = (value << 8) | p[i];
	return value;
}

static bool
valid_frame(const uint8_t *frame, uint32_t size)
{
	return frame != NULL && size >= 16 && someip_read_be(frame + 4, 4) == size - 8;
}

typedef struct {
	EVP_MD_CTX *ctx;
	bool wide_index;
	size_t used;
	uint8_t buffer[65536];
} someip_hash;

static void
someip_hash_free(someip_hash *hash)
{
	if (hash != NULL) {
		EVP_MD_CTX_free(hash->ctx);
		nng_free(hash, sizeof(*hash));
	}
}

static int
someip_hash_update(someip_hash *hash, const uint8_t *data, size_t size)
{
	while (size > 0) {
		size_t n = sizeof(hash->buffer) - hash->used;
		if (n > size) {
			n = size;
		}
		memcpy(hash->buffer + hash->used, data, n);
		hash->used += n;
		data += n;
		size -= n;
		if (hash->used == sizeof(hash->buffer)) {
			if (EVP_DigestUpdate(hash->ctx, hash->buffer, hash->used) != 1) {
				return -1;
			}
			hash->used = 0;
		}
	}
	return 0;
}

static void
someip_write_be(uint8_t *p, uint64_t value, uint32_t width)
{
	for (uint32_t i = 0; i < width; i++) {
		p[i] = (uint8_t) (value >> (8 * (width - i - 1)));
	}
}

static someip_hash *
someip_hash_begin(uint32_t count, bool wide_index)
{
	const char *domain = wide_index ? "NanoMQ SOME/IP batch v3" :
	    "NanoMQ SOME/IP batch v2";
	uint8_t rows[4];
	someip_hash *hash = nng_alloc(sizeof(*hash));
	if (hash == NULL) {
		return NULL;
	}
	hash->ctx = EVP_MD_CTX_new();
	hash->used = 0;
	hash->wide_index = wide_index;
	someip_write_be(rows, count, sizeof(rows));
	if (hash->ctx == NULL || EVP_DigestInit_ex(hash->ctx, EVP_sha256(), NULL) != 1 ||
	    someip_hash_update(hash, (const uint8_t *) domain, strlen(domain)) != 0 ||
	    someip_hash_update(hash, rows, sizeof(rows)) != 0) {
		someip_hash_free(hash);
		return NULL;
	}
	return hash;
}

static int
someip_hash_record(someip_hash *hash, uint64_t ts, uint64_t batch_index,
    uint32_t frame_index, const uint8_t *frame, uint32_t size, uint16_t session)
{
	/* A fixed-width prefix makes lengths, order and scalar values unambiguous.
	 * Rebuild only the header: template and original wire bytes hash alike. */
	uint8_t prefix[40];
	uint32_t width = hash->wide_index ? 8 : 4;
	uint32_t header_offset = 16 + width;
	someip_write_be(prefix, ts, 8);
	someip_write_be(prefix + 8, batch_index, width);
	someip_write_be(prefix + 8 + width, frame_index, 4);
	someip_write_be(prefix + 12 + width, size, 4);
	memcpy(prefix + header_offset, frame, 16u);
	someip_write_be(prefix + header_offset + 10, session, 2);
	return someip_hash_update(hash, prefix, header_offset + 16u) == 0 &&
	    someip_hash_update(hash, frame + 16u,
	        size - 16u) == 0 ? 0 : -1;
}

static int
someip_hash_finish(someip_hash *hash, uint32_t count,
    char manifest[SOMEIP_MANIFEST_SIZE])
{
	static const char hex[] = "0123456789abcdef";
	uint8_t digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	if (EVP_DigestUpdate(hash->ctx, hash->buffer, hash->used) != 1 ||
	    EVP_DigestFinal_ex(hash->ctx, digest, &size) != 1 || size != 32) {
		return -1;
	}
	int n = snprintf(manifest, SOMEIP_MANIFEST_SIZE, "someip-v%u:%08" PRIx32 ":",
	    hash->wide_index ? 3u : 2u, count);
	if (n != 19) {
		return -1;
	}
	for (uint32_t i = 0; i < size; i++) {
		manifest[n++] = hex[digest[i] >> 4];
		manifest[n++] = hex[digest[i] & 15];
	}
	manifest[n] = '\0';
	return 0;
}

someip_batch *
someip_batch_alloc(uint32_t rows, uint32_t bytes)
{
	if (rows == 0 || bytes > INT32_MAX || (uint64_t) rows * 16 > bytes ||
	    (uint64_t) rows * sizeof(uint64_t) > SIZE_MAX ||
	    ((uint64_t) rows + 1) * sizeof(uint32_t) > SIZE_MAX) return NULL;
	someip_batch *b = nng_zalloc(sizeof(*b));
	if (b == NULL) return NULL;
	b->rows = rows;
	b->template_size = bytes;
	b->end_key = INT64_MAX;
	b->ts = nng_alloc((size_t) rows * sizeof(*b->ts));
	b->batch_index = nng_alloc((size_t) rows * sizeof(*b->batch_index));
	b->frame_index = nng_alloc((size_t) rows * sizeof(*b->frame_index));
	b->session_id = nng_alloc((size_t) rows * sizeof(*b->session_id));
	b->offsets = nng_alloc(((size_t) rows + 1) * sizeof(*b->offsets));
	b->templates = nng_alloc(bytes);
	if (!b->ts || !b->batch_index || !b->frame_index || !b->session_id ||
	    !b->offsets || !b->templates) {
		someip_batch_free(b);
		return NULL;
	}
	b->offsets[0] = 0;
	b->offsets[rows] = bytes;
	return b;
}

void
someip_batch_free(void *data)
{
	someip_batch *b = data;
	if (b == NULL) return;
	nng_free(b->ts, (size_t) b->rows * sizeof(*b->ts));
	nng_free(b->batch_index, (size_t) b->rows * sizeof(*b->batch_index));
	nng_free(b->frame_index, (size_t) b->rows * sizeof(*b->frame_index));
	nng_free(b->session_id, (size_t) b->rows * sizeof(*b->session_id));
	nng_free(b->offsets, ((size_t) b->rows + 1) * sizeof(*b->offsets));
	nng_free(b->templates, b->template_size);
	nng_free(b, sizeof(*b));
}

char **
someip_batch_schema(const someip_batch *b)
{
	static char *plain[] = { "ts", "batch_index", "frame_index", "session_id", "message_template" };
	static char *v2[] = { "ts", "batch_index", "frame_index", "session_id", "message_template_v2" };
	static char *v3[] = { "ts", "batch_index", "frame_index", "session_id", "message_template_v3" };
	return b->version == 3 ? v3 : b->version == 2 ? v2 : plain;
}

typedef struct {
	uint32_t rows;
	uint32_t bytes;
	uint16_t previous_session, step;
	bool stride, wide;
} someip_stats;

static int
observe(someip_stats *s, const uint8_t *frame, uint32_t size, uint64_t key)
{
	if (!valid_frame(frame, size) || key > INT64_MAX || s->rows == UINT32_MAX ||
	    size > INT32_MAX - s->bytes) return -1;
	uint16_t session = (uint16_t) someip_read_be(frame + 10, 2);
	uint16_t step = (uint16_t) (session - s->previous_session);
	if (s->stride) {
		if (s->rows == 1) s->step = step;
		else if (s->rows > 1 && s->step != step) s->stride = false;
	}
	s->previous_session = session;
	s->rows++;
	s->bytes += size;
	s->wide |= key > UINT32_MAX;
	return 0;
}

static someip_batch *
begin_batch(const someip_stats *s, bool checked, someip_hash **hash)
{
	someip_batch *b = someip_batch_alloc(s->rows, s->bytes);
	if (b == NULL) return NULL;
	b->session_delta = s->stride;
	b->version = checked ? (s->wide ? 3 : 2) : 0;
	*hash = checked ? someip_hash_begin(s->rows, s->wide) : NULL;
	if (checked && *hash == NULL) {
		someip_batch_free(b);
		return NULL;
	}
	return b;
}

static int
append_frame(someip_batch *b, someip_hash *hash, uint32_t row,
    uint64_t ts, uint64_t key, uint32_t index, const uint8_t *frame, uint32_t size)
{
	b->ts[row] = ts;
	b->batch_index[row] = key;
	b->frame_index[row] = index;
	b->session_id[row] = (int32_t) someip_read_be(frame + 10, 2);
	uint8_t *out = b->templates + b->offsets[row];
	memcpy(out, frame, size);
	out[10] = out[11] = 0;
	b->offsets[row + 1] = b->offsets[row] + size;
	return hash == NULL ? 0 : someip_hash_record(hash, ts, key, index,
	    frame, size, (uint16_t) b->session_id[row]);
}

static parquet_data *
finish_batch(someip_batch *b, someip_hash *hash)
{
	bool ok = hash == NULL || someip_hash_finish(hash, b->rows, b->manifest) == 0;
	someip_hash_free(hash);
	parquet_data *result = ok ? parquet_data_alloc_someip(b) : NULL;
	if (result == NULL) someip_batch_free(b);
	return result;
}

parquet_data *
someip_encode_records(const someip_record *records, uint32_t count, bool checked)
{
	someip_stats s = { .stride = true };
	if (records == NULL || count == 0) return NULL;
	for (uint32_t r = 0; r < count; r++) {
		const someip_record *p = &records[r];
		/* Equal keys represent frames in one window. Descending keys would
		 * make the first/last-key filename range hide records from queries. */
		if (p->ts > INT64_MAX ||
		    (r && p->batch_index < records[r - 1].batch_index) ||
		    observe(&s, p->frame, p->size, p->batch_index) != 0) return NULL;
	}
	someip_hash *hash = NULL;
	someip_batch *b = begin_batch(&s, checked, &hash);
	if (b == NULL) return NULL;
	for (uint32_t r = 0; r < count; r++) {
		const someip_record *p = &records[r];
		if (append_frame(b, hash, r, p->ts, p->batch_index, p->frame_index,
		        p->frame, p->size) != 0) {
			someip_hash_free(hash);
			someip_batch_free(b);
			return NULL;
		}
	}
	return finish_batch(b, hash);
}

static const uint8_t *
window_payload(const struct stream_data_in *in, uint32_t i, uint32_t *size)
{
	if (in->datas != NULL && in->lens != NULL) {
		*size = in->lens[i];
		return in->datas[i];
	}
	return in->msgs != NULL ? stream_msg_payload(in->msgs[i], size) : NULL;
}

static void *
encode_windows(void *data, bool checked)
{
	const struct stream_data_in *in = data;
	someip_stats s = { .stride = true };
	if (in == NULL || in->len == 0 || in->keys == NULL ||
	    ((in->datas == NULL) != (in->lens == NULL))) return NULL;
	/* Validate the entire input before allocating or producing output. */
	for (uint32_t w = 0; w < in->len; w++) {
		uint32_t size = 0, pos = 4;
		const uint8_t *p = window_payload(in, w, &size);
		if (p == NULL || size < 4 || in->keys[w] > INT64_MAX ||
		    (w && in->keys[w] <= in->keys[w - 1])) return NULL;
		uint32_t count = someip_read_be(p, 4);
		if (count == 0 || count > (size - 4) / 22) return NULL;
		for (uint32_t i = 0; i < count; i++) {
			if (size - pos < 6) return NULL;
			uint32_t n = someip_read_be(p + pos + 4, 2);
			pos += 6;
			if (n > size - pos || observe(&s, p + pos, n, in->keys[w]) != 0) return NULL;
			pos += n;
		}
		if (pos != size) return NULL;
	}
	someip_hash *hash = NULL;
	someip_batch *b = begin_batch(&s, checked, &hash);
	if (b == NULL) return NULL;
	uint32_t row = 0;
	for (uint32_t w = 0; w < in->len; w++) {
		uint32_t size = 0, pos = 4;
		const uint8_t *p = window_payload(in, w, &size);
		uint32_t count = someip_read_be(p, 4);
		for (uint32_t f = 0; f < count; f++) {
			uint32_t ts = someip_read_be(p + pos, 4);
			uint32_t n = someip_read_be(p + pos + 4, 2);
			pos += 6;
			if (append_frame(b, hash, row++, ts, in->keys[w], f, p + pos, n) != 0) {
				someip_hash_free(hash);
				someip_batch_free(b);
				return NULL;
			}
			pos += n;
		}
	}
	return finish_batch(b, hash);
}

void *someip_encode(void *data) { return encode_windows(data, false); }
void *someip_checked_encode(void *data) { return encode_windows(data, true); }

int
someip_batch_validate(const someip_batch *b)
{
	if (b == NULL || b->rows == 0 || b->ts == NULL || b->batch_index == NULL ||
	    b->frame_index == NULL || b->session_id == NULL || b->offsets == NULL ||
	    b->templates == NULL || b->template_size > INT32_MAX ||
	    b->offsets[0] != 0 || b->offsets[b->rows] != b->template_size ||
	    (b->version != 0 && b->version != 2 && b->version != 3) ||
	    (b->version == 0 && b->manifest[0] != '\0') ||
	    (b->version != 0 && b->manifest[83] != '\0')) return -1;
	someip_hash *hash = b->version ? someip_hash_begin(b->rows, b->version == 3) : NULL;
	if (b->version && hash == NULL) return -1;
	int result = -1;
	for (uint32_t r = 0; r < b->rows; r++) {
		if (b->ts[r] > INT64_MAX || b->batch_index[r] > INT64_MAX ||
		    (b->version == 2 && b->batch_index[r] > UINT32_MAX) ||
		    b->frame_index[r] > UINT32_MAX || b->session_id[r] < 0 ||
		    b->session_id[r] > UINT16_MAX || b->offsets[r + 1] < b->offsets[r] ||
		    b->offsets[r + 1] > b->template_size) goto done;
		const uint8_t *p = b->templates + b->offsets[r];
		uint32_t n = b->offsets[r + 1] - b->offsets[r];
		if (!valid_frame(p, n) || p[10] != 0 || p[11] != 0) goto done;
		if (hash && someip_hash_record(hash, b->ts[r], b->batch_index[r],
		        (uint32_t) b->frame_index[r], p, n, (uint16_t) b->session_id[r]) != 0) goto done;
	}
	char manifest[84];
	if (hash && (someip_hash_finish(hash, b->rows, manifest) != 0 ||
	        memcmp(manifest, b->manifest, sizeof(manifest)) != 0)) goto done;
	result = 0;
done:
	someip_hash_free(hash);
	return result;
}

static void
hex_byte(char **out, uint8_t c)
{
	static const char hex[] = "0123456789abcdef";
	*(*out)++ = hex[c >> 4];
	*(*out)++ = hex[c & 15];
}

void *
someip_batch_decode(const someip_batch *b, uint64_t start, uint64_t end)
{
	if (start > end || someip_batch_validate(b) != 0) return NULL;
	uint64_t bytes = 1;
	for (uint32_t r = 0; r < b->rows; r++) {
		if (b->batch_index[r] < start || b->batch_index[r] > end) continue;
		bytes += 128 + 2 * (uint64_t) (b->offsets[r + 1] - b->offsets[r]);
		if (bytes > UINT32_MAX || bytes > SIZE_MAX) return NULL;
	}
	struct stream_decoded_data *result = nng_zalloc(sizeof(*result));
	if (result == NULL) return NULL;
	result->data = nng_alloc((size_t) bytes);
	if (result->data == NULL) { nng_free(result, sizeof(*result)); return NULL; }
	char *out = result->data;
	for (uint32_t r = 0; r < b->rows; r++) {
		if (b->batch_index[r] < start || b->batch_index[r] > end) continue;
		int n = snprintf(out, (size_t) bytes - (out - (char *) result->data),
		    "ts=%" PRIu64 ",batch_index=%" PRIu64 ",frame_index=%" PRIu64 ",someip=",
		    b->ts[r], b->batch_index[r], b->frame_index[r]);
		if (n < 0 || n >= 128) { stream_decoded_data_free(result); return NULL; }
		out += n;
		const uint8_t *p = b->templates + b->offsets[r];
		uint32_t len = b->offsets[r + 1] - b->offsets[r];
		for (uint32_t i = 0; i < len; i++) {
			uint8_t value = i == 10 ? (uint8_t) (b->session_id[r] >> 8) :
			    i == 11 ? (uint8_t) b->session_id[r] : p[i];
			hex_byte(&out, value);
		}
		*out++ = '\n';
	}
	*out = '\0';
	result->len = (uint32_t) (out - (char *) result->data);
	return result;
}

void *
someip_decode_range(parquet_data *data, uint64_t start, uint64_t end)
{
	struct stream_data_out *view = (struct stream_data_out *) data;
	return view ? someip_batch_decode(view->someip_data, start, end) : NULL;
}

void *
someip_decode(void *data)
{
	parquet_data_ret *ret = data;
	if (ret == NULL || ret->someip_data == NULL) return NULL;
	const someip_batch *b = ret->someip_data;
	return someip_batch_decode(b, b->start_key, b->end_key);
}

static int
parse_key(const char **p, uint64_t *key)
{
	uint64_t value = 0;
	if (**p < '0' || **p > '9') return -1;
	while (**p >= '0' && **p <= '9') {
		uint32_t digit = (uint32_t) (*(*p)++ - '0');
		if (value > ((uint64_t) INT64_MAX - digit) / 10) return -1;
		value = value * 10 + digit;
	}
	*key = value;
	return 0;
}

void *
someip_cmd_parser(void *data)
{
	const char *p = data;
	bool sync;
	if (p == NULL) return NULL;
	if (strncmp(p, "sync-", 5) == 0) { sync = true; p += 5; }
	else if (strncmp(p, "async-", 6) == 0) { sync = false; p += 6; }
	else return NULL;
	uint64_t start, end;
	if (parse_key(&p, &start) != 0 || *p++ != '-' ||
	    parse_key(&p, &end) != 0 || *p != '\0' || start > end) return NULL;
	struct cmd_data *cmd = nng_zalloc(sizeof(*cmd));
	if (cmd != NULL) { cmd->is_sync = sync; cmd->start_key = start; cmd->end_key = end; }
	return cmd;
}

int
someip_stream_register(void)
{
	char *name = nng_strdup("someip");
	if (name == NULL) return NNG_ENOMEM;
	int rv = stream_register(name, SOMEIP_STREAM_ID, someip_decode, someip_encode, someip_cmd_parser);
	if (rv != 0) { nng_strfree(name); return rv; }
	name = nng_strdup("someip_checked");
	if (name == NULL) { stream_unregister(SOMEIP_STREAM_ID); return NNG_ENOMEM; }
	rv = stream_register(name, SOMEIP_CHECKED_STREAM_ID, someip_decode, someip_checked_encode, someip_cmd_parser);
	if (rv != 0) { nng_strfree(name); stream_unregister(SOMEIP_STREAM_ID); }
	return rv;
}
