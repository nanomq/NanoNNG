#include "core/nng_impl.h"
#include "nng/nng.h"
#include "supplemental/mqtt/mqtt_msg.h"
#include "supplemental/sqlite/sqlite3.h"

static uint8_t *nni_msg_serialize(nni_msg *msg, size_t *out_len);
static nni_msg *nni_msg_deserialize(uint8_t *bytes, size_t len);

static char *table = NULL;

int
nni_mqtt_qos_db_init(sqlite3 **db, const char *db_name, const char *table_name)
{
	int rv;
	if ((rv = sqlite3_open(db_name, db)) != 0) {
		return rv;
	}

	size_t len = 80 + strlen(table_name);
	char * sql = nng_zalloc(len);
	sqlite3_snprintf(len, sql,
	    "CREATE TABLE IF NOT EXISTS %s ("
	    "id INTEGER PRIMARY KEY NOT NULL, "
	    "msg BLOB )",
	    table_name);

	if ((rv = sqlite3_exec(*db, sql, 0, 0, 0) != 0)) {
		goto out;
	}
	table = nng_strdup(table_name);

out:
	if (sql) {
		nng_free(sql, len);
	}
	return rv;
}

int
nni_mqtt_qos_db_close(sqlite3 *db)
{
	if (table) {
		nng_strfree(table);
	}
	return sqlite3_close(db);
}

int
nni_mqtt_qos_db_set(sqlite3 *db, uint32_t id, nni_msg *msg)
{
	sqlite3_stmt *stmt;
	size_t        len = strlen(table) + 50;
	char *        sql = nng_zalloc(len);
	sqlite3_snprintf(
	    len, sql, "REPLACE INTO  %s (id, msg) VALUES (?,?)", table);
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, id);
	// serialize nni_msg to a bytes.
	size_t   nbyte = 0;
	uint8_t *bytes = nni_msg_serialize(msg, &nbyte);
	if (nbyte > 0) {
		sqlite3_bind_blob64(stmt, 2, bytes, nbyte, SQLITE_TRANSIENT);
	} else {
		sqlite3_bind_null(stmt, 2);
	}
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	nng_free(bytes, nbyte);

	return sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

nni_msg *
nni_mqtt_qos_db_get(sqlite3 *db, uint32_t id)
{
	nni_msg *     msg = NULL;
	sqlite3_stmt *stmt;
	size_t        len = strlen(table) + 60;
	char *        sql = nng_zalloc(len);
	sqlite3_snprintf(
	    len, sql, "SELECT id, msg FROM %s WHERE id = ? LIMIT 1", table);
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int64(stmt, 1, id);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		uint32_t pipe_id = (uint32_t) sqlite3_column_int64(stmt, 0);
		if (id == pipe_id) {
			size_t nbyte =
			    (size_t) sqlite3_column_bytes16(stmt, 1);
			uint8_t *bytes = sqlite3_malloc(nbyte);
			memcpy(bytes, sqlite3_column_blob(stmt, 1), nbyte);
			// deserialize blob data to nni_msg
			msg = nni_msg_deserialize(bytes, nbyte);
			sqlite3_free(bytes);
		}
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	return msg;
}

int
nni_mqtt_qos_db_remove(sqlite3 *db, uint32_t id)
{
	int           rv;
	sqlite3_stmt *stmt;
	size_t        len = strlen(table) + 50;
	char *        sql = nng_zalloc(len);
	sqlite3_snprintf(len, sql, "DELETE FROM %s WHERE id = ?", table);
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int(stmt, 1, id);
	sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

int
nni_mqtt_qos_db_foreach(sqlite3 *db)
{
	return 0;
}

static uint8_t *
nni_msg_serialize(nni_msg *msg, size_t *out_len)
{
	size_t len = nni_msg_header_len(msg) + nni_msg_len(msg) +
	    (sizeof(uint32_t) * 2);
	*out_len = len;

	// bytes:
	// header:  header_len(uint32) + header(header_len)
	// body:	body_len(uint32) + body(body_len)
	uint8_t *bytes = nng_zalloc(len);

	struct pos_buf buf = { .curpos = &bytes[0], .endpos = &bytes[len] };

	if (write_uint32(nni_msg_header_len(msg), &buf) != 0) {
		goto out;
	}
	if (write_bytes(nni_msg_header(msg), nni_msg_header_len(msg), &buf) !=
	    0) {
		goto out;
	}
	if (write_uint32(nni_msg_len(msg), &buf) != 0) {
		goto out;
	}
	if (write_bytes(nni_msg_body(msg), nni_msg_len(msg), &buf) != 0) {
		goto out;
	}

	return bytes;

out:
	free(bytes);
	return NULL;
}

static nni_msg *
nni_msg_deserialize(uint8_t *bytes, size_t len)
{
	nni_msg *msg;
	if (nni_msg_alloc(&msg, 0) != 0) {
		return NULL;
	}

	struct pos_buf buf = { .curpos = &bytes[0], .endpos = &bytes[len] };

	// bytes:
	// header:  header_len(uint32) + header(header_len)
	// body:	body_len(uint32) + body(body_len)

	uint32_t header_len;
	if (read_uint32(&buf, &header_len) != 0) {
		goto out;
	}
	nni_msg_header_append(msg, buf.curpos, header_len);
	buf.curpos += header_len;

	uint32_t body_len;
	if (read_uint32(&buf, &body_len) != 0) {
		goto out;
	}
	nni_msg_append(msg, buf.curpos, body_len);

	return msg;

out:
	nni_msg_free(msg);
	return NULL;
}