#include "nng/nng.h"
#include "mqtt_qos_db.h"
#include "nng/supplemental/nanolib/log.h"
#include "core/nng_impl.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/supplemental/sqlite/sqlite3.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/khash.h"
#include "supplemental/mqtt/mqtt_msg.h"
#include <string.h>
#include <stdlib.h>

#define table_main "t_main"
#define table_msg "t_msg"
#define table_pipe_client "t_pipe_client"
#define table_client_msg "t_client_msg"
#define table_client_offline_msg "t_client_offline_msg"
#define table_client_info "t_client_info"
#define table_retain "t_retain"

static uint8_t *nni_msg_serialize(nni_msg *msg, size_t *out_len);
static nni_msg *nni_msg_deserialize(uint8_t *bytes, size_t len);
static uint8_t *nni_mqtt_msg_serialize(
    nni_msg *msg, size_t *out_len, uint8_t proto_ver);
static nni_msg *nni_mqtt_msg_deserialize(
    uint8_t *bytes, size_t len, bool aio_available, uint8_t proto_ver);
static int      create_msg_table(sqlite3 *db);
static int      create_pipe_client_table(sqlite3 *db);
static int      create_main_table(sqlite3 *db);
static int      create_client_msg_table(sqlite3 *db);
static int      create_client_offline_msg_table(sqlite3 *db);
static int      create_client_info_table(sqlite3 *db);
static int      create_retain_msg_table(sqlite3 *db);
static char *   get_db_path(
       char *dest_path, const char *user_path, const char *db_name);
static void    set_db_pragma(sqlite3 *db);
static void    remove_oldest_msg(
       sqlite3 *db, const char *table_name, const char *col_name, uint64_t limit) NNG_DEPRECATED;
static void    remove_oldest_client_msg(sqlite3 *db, const char *table_name,
       const char *col_name, uint64_t limit, const char *config_name);
static int64_t get_id_by_msg(sqlite3 *db, nni_msg *msg);
static int64_t insert_msg(sqlite3 *db, nni_msg *msg);
static int64_t get_id_by_pipe(sqlite3 *db, uint32_t pipe_id);
static int64_t get_id_by_client_id(sqlite3 *db, const char *client_id);
static int     get_id_by_p_id(sqlite3 *db, int64_t p_id, uint16_t packet_id,
        uint8_t *out_qos, int64_t *out_m_id);
static int     get_client_info_id(sqlite3 *db, const char *config_name);
static int     insert_main(
        sqlite3 *db, int64_t p_id, uint16_t packet_id, uint8_t qos, int64_t m_id);
static int update_main(
    sqlite3 *db, int64_t p_id, uint16_t packet_id, uint8_t qos, int64_t m_id);
static void set_main(sqlite3 *db, uint32_t pipe_id, uint16_t packet_id,
    uint8_t qos, nni_msg *msg);

// ---------------------------------------------------------------------------
// Write-behind buffer for the retained-message store.
//
// nni_mqtt_qos_db_set_retain() used to commit one transaction per retained
// publish, synchronously on the publishing thread.  Instead, mutations are
// buffered keyed by topic and a single flush thread commits the accumulated
// batch in one transaction, triggered by the batch reaching `threshold`
// entries or `interval_ms` milliseconds elapsing, whichever comes first.
//
// Only the last operation per topic is kept, so a store followed by a
// removal inside one flush window cancels out and never reaches disk.  Reads
// consult the buffer first (or flush ahead of a pattern query), so a caller
// always observes its own writes.  Batching is off unless the broker turns it
// on, which leaves the synchronous path in place for every other user of this
// database, and at most one database per process may be batched -- the buffer
// and its flush thread are process-global.  See
// docs/adr/0005-retain-write-behind-buffer.md.
// ---------------------------------------------------------------------------

typedef struct nni_retain_op {
	char    *topic; // owned copy, and the khash key itself
	uint8_t  proto_ver;
	uint8_t *blob;  // serialized message; NULL marks a removal (tombstone)
	size_t   len;
} nni_retain_op;

// topic string -> nni_retain_op *
KHASH_MAP_INIT_STR(retain_ops, nni_retain_op *)

typedef struct nni_retain_batch {
	bool           on;
	bool           initialized; // set once; the lock/buffer are never freed
	bool           warned_off;  // one warning per runtime-disable attempt
	sqlite3       *db;
	sqlite3       *flush_db;    // owned connection the flush thread writes on
	conf_sqlite   *conf;        // live broker config, re-read on every tick
	nni_mtx        lk;
	nni_cv         cv;
	nni_thr        thr;
	bool           stop;
	size_t         threshold;   // flush once this many entries are pending
	uint64_t       interval_ms; // ... or once this long has elapsed
	size_t         hard_limit;  // flush inline beyond this, to bound memory
	khash_t(retain_ops) *ops;
	size_t         count; // pending entries
} nni_retain_batch;

static nni_retain_batch g_retain;

static void retain_batch_flush(void);
static void retain_batch_shutdown(void);

// Returned by retain_batch_enqueue()/remove() when batching was switched off
// between the caller's unlocked check and the buffer lock, so the caller has
// to use the synchronous path instead.  Distinct from 0 (handled) and -1.
#define RETAIN_BATCH_SYNC 1

static int
create_client_msg_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_client_msg ""
	             " (id INTEGER PRIMARY KEY AUTOINCREMENT, "
	             "  packet_id INTEGER NOT NULL, "
	             "  pipe_id INTEGER NOT NULL, "
	             "  data BLOB, "
	             "  info_id INTEGER NOT NULL,"
				 "  proto_ver TINYINT DEFAULT 4,"
	             "  ts DATETIME DEFAULT CURRENT_TIMESTAMP )";

	return sqlite3_exec(db, sql, 0, 0, 0);
}

static int
create_client_offline_msg_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_client_offline_msg ""
	             " (id INTEGER PRIMARY KEY AUTOINCREMENT, "
	             "  data BLOB, "
	             "  info_id INTEGER NOT NULL, "
				 "  proto_ver TINYINT DEFAULT 4, "
	             "  ts DATETIME DEFAULT CURRENT_TIMESTAMP )";

	return sqlite3_exec(db, sql, 0, 0, 0);
}

static int
create_client_info_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_client_info ""
	             " (id INTEGER PRIMARY KEY AUTOINCREMENT, "
	             "  config_name TEXT NOT NULL UNIQUE, "
	             "  client_id TEXT , "
	             "  proto_name TEXT , "
	             "  proto_ver TINY INT , "
	             "  ts DATETIME DEFAULT CURRENT_TIMESTAMP )";

	return sqlite3_exec(db, sql, 0, 0, 0);
}

static int
create_msg_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_msg ""
	             " (id INTEGER PRIMARY KEY AUTOINCREMENT, "
	             "  data BLOB)";

	return sqlite3_exec(db, sql, 0, 0, 0);
}

static int
create_pipe_client_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_pipe_client ""
	             "(id INTEGER PRIMARY KEY  AUTOINCREMENT, "
	             " pipe_id    INTEGER NOT NULL, "
	             " client_id  TEXT NOT NULL)";
	return sqlite3_exec(db, sql, 0, 0, 0);
}

static int
create_main_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_main ""
	             "(id INTEGER PRIMARY KEY  AUTOINCREMENT,"
	             " p_id INTEGER NOT NULL, "
	             " packet_id INTEGER NOT NULL, "
	             " qos  TINYINT NOT NULL , "
	             " m_id INTEGER NOT NULL , "
	             " ts DATETIME DEFAULT CURRENT_TIMESTAMP "
	             " )";

	return sqlite3_exec(db, sql, 0, 0, 0);
}

static int
create_retain_msg_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_retain ""
	             "(topic TEXT PRIMARY KEY NOT NULL, "
				 "proto_ver TINYINT NOT NULL, "
	             "msg BLOB)";

	return sqlite3_exec(db, sql, 0, 0, 0);
}

static void
set_db_pragma(sqlite3 *db)
{
	sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, 0, 0);
	sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, 0, 0);
	sqlite3_exec(db, "PRAGMA wal_autocheckpoint", NULL, 0, 0);
}

static char *
get_db_path(char *dest_path, const char *user_path, const char *db_name)
{
	if (user_path == NULL) {
		char pwd[512] = { 0 };
		if (nni_plat_getcwd(pwd, sizeof(pwd)) != NULL) {
			sprintf(dest_path, "%s/%s", pwd, db_name);
		} else {
			return NULL;
		}
	} else {
		if (user_path[strlen(user_path) - 1] == '/') {
			sprintf(dest_path, "%s%s", user_path, db_name);
		} else {
			sprintf(dest_path, "%s/%s", user_path, db_name);
		}
	}

	return dest_path;
}

void
nni_mqtt_qos_db_init(sqlite3 **db, const char *user_path, const char *db_name, bool is_broker)
{
	char db_path[1024] = { 0 };

	int rv = 0;

	if (NULL != get_db_path(db_path, user_path, db_name) &&
	    ((rv = sqlite3_open(db_path, db)) != 0)) {
		nni_panic("Can't open database %s: %s\n", db_path,
		    sqlite3_errmsg(*db));
		return;
	}
	set_db_pragma(*db);
	if (is_broker) {
		if (create_msg_table(*db) != 0) {
			return;
		}
		if (create_pipe_client_table(*db) != 0) {
			return;
		}
		if (create_main_table(*db) != 0) {
			return;
		}
		if (create_retain_msg_table(*db) != 0) {
			return;
		}
	} else {
		if (create_client_msg_table(*db) != 0) {
			return;
		}
		if (create_client_offline_msg_table(*db) != 0) {
			return;
		}
		if (create_client_info_table(*db) != 0) {
			return;
		}
	}
}

void
nni_mqtt_qos_db_close(sqlite3 *db)
{
	// Only the batched database owns the buffer -- see the one-database
	// constraint documented on nni_mqtt_qos_db_retain_batch_setup().  This
	// function is also called for client and bridge databases
	// (nni_mqtt_sqlite_db_fini), which run in the same process as the
		// broker, so "is this the batched handle?" is part of the invariant
		// rather than a defensive check.
	if (g_retain.on && g_retain.db == db) {
		retain_batch_shutdown();
	}
	sqlite3_close(db);
}

static int64_t
get_id_by_msg(sqlite3 *db, nni_msg *msg)
{
	int64_t       id = 0;
	sqlite3_stmt *stmt;
	size_t        len   = 0;
	uint8_t *     blob  = nni_msg_serialize(msg, &len);
	char          sql[] = "SELECT id FROM " table_msg " where data = ?";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_blob64(stmt, 1, blob, len, SQLITE_TRANSIENT);
	if (SQLITE_ROW == sqlite3_step(stmt)) {
		id = sqlite3_column_int64(stmt, 0);
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	nng_free(blob, len);
	return id;
}

static int64_t
insert_msg(sqlite3 *db, nni_msg *msg)
{
	int64_t       id = 0;
	sqlite3_stmt *stmt;
	char *        sql = "INSERT INTO  " table_msg " (data) VALUES (?)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	size_t   len  = 0;
	uint8_t *blob = nni_msg_serialize(msg, &len);
	sqlite3_bind_blob64(stmt, 1, blob, len, SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	nng_free(blob, len);
	id = sqlite3_last_insert_rowid(db);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	return id;
}

static int64_t
get_id_by_pipe(sqlite3 *db, uint32_t pipe_id)
{
	int64_t       id = 0;
	sqlite3_stmt *stmt;
	char sql[] = "SELECT id FROM " table_pipe_client " WHERE pipe_id = ?";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int64(stmt, 1, pipe_id);
	if (SQLITE_ROW == sqlite3_step(stmt)) {
		id = sqlite3_column_int64(stmt, 0);
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	return id;
}

static int64_t
get_id_by_client_id(sqlite3 *db, const char *client_id)
{
	int64_t       id = 0;
	sqlite3_stmt *stmt;
	char          sql[] =
	    "SELECT id FROM " table_pipe_client " WHERE client_id = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_text(
	    stmt, 1, client_id, strlen(client_id), SQLITE_TRANSIENT);
	if (SQLITE_ROW == sqlite3_step(stmt)) {
		id = sqlite3_column_int64(stmt, 0);
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	return id;
}

static int
get_id_by_p_id(sqlite3 *db, int64_t p_id, uint16_t packet_id, uint8_t *out_qos,
    int64_t *out_m_id)
{
	int64_t       id = 0;
	sqlite3_stmt *stmt;
	char          sql[] = "SELECT id, qos, m_id FROM " table_main
	             " WHERE p_id = ? AND packet_id = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int64(stmt, 1, p_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	if (SQLITE_ROW == sqlite3_step(stmt)) {
		id        = sqlite3_column_int64(stmt, 0);
		*out_qos  = sqlite3_column_int(stmt, 1);
		*out_m_id = sqlite3_column_int64(stmt, 2);
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	return id;
}

static int
insert_main(
    sqlite3 *db, int64_t p_id, uint16_t packet_id, uint8_t qos, int64_t m_id)
{
	sqlite3_stmt *stmt;
	char *        sql = "INSERT INTO " table_main ""
	            " (p_id, packet_id, qos, m_id) VALUES (?, ?, ?, ?)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, p_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	sqlite3_bind_int(stmt, 3, qos);
	sqlite3_bind_int64(stmt, 4, m_id);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	return sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

static int
update_main(
    sqlite3 *db, int64_t p_id, uint16_t packet_id, uint8_t qos, int64_t m_id)
{
	sqlite3_stmt *stmt;
	char *        sql = "UPDATE " table_main ""
	            " SET qos = ?, m_id = ? WHERE p_id = ? AND packet_id = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int(stmt, 1, qos);
	sqlite3_bind_int64(stmt, 2, m_id);
	sqlite3_bind_int64(stmt, 3, p_id);
	sqlite3_bind_int64(stmt, 4, packet_id);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	return sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

static void
remove_oldest_msg(
    sqlite3 *db, const char *table_name, const char *col_name, uint64_t limit)
{
	sqlite3_stmt *stmt;
	char          sql[256] = { 0 };

	snprintf(sql, 256,
	    "DELETE FROM %s WHERE %s NOT IN ( SELECT %s FROM %s ORDER BY"
	    " %s DESC LIMIT ?)",
	    table_name, col_name, col_name, table_name, col_name);

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, limit);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_oldest(sqlite3 *db, uint64_t limit)
{
	remove_oldest_msg(db, table_main, "ts", limit);
}

void
nni_mqtt_qos_db_insert_pipe(
    sqlite3 *db, uint32_t pipe_id, const char *client_id)
{
	sqlite3_stmt *stmt;
	char *        sql = "INSERT INTO " table_pipe_client ""
	            " (pipe_id, client_id) VALUES (?, ?)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_text(
	    stmt, 2, client_id, strlen(client_id), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_pipe(sqlite3 *db, uint32_t pipe_id)
{
	sqlite3_stmt *stmt;
	char *        sql = "DELETE FROM " table_pipe_client ""
	            " where pipe_id = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_update_pipe_by_clientid(
    sqlite3 *db, uint32_t pipe_id, const char *client_id)
{
	sqlite3_stmt *stmt;
	char *        sql = "UPDATE " table_pipe_client " SET pipe_id = ?"
	            " where client_id = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_text(
	    stmt, 2, client_id, strlen(client_id), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_set_pipe(sqlite3 *db, uint32_t pipe_id, const char *client_id)
{
	int64_t id = get_id_by_client_id(db, client_id);
	if (id == 0) {
		nni_mqtt_qos_db_insert_pipe(db, pipe_id, client_id);
	} else {
		// TODO: now clientid always match with pipe_id
		nni_mqtt_qos_db_update_pipe_by_clientid(
		    db, pipe_id, client_id);
	}
}

void
nni_mqtt_qos_db_update_all_pipe(sqlite3 *db, uint32_t pipe_id)
{
	sqlite3_stmt *stmt;
	char *        sql = "UPDATE " table_pipe_client " SET pipe_id = ?"
	            " where id > 0";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_msg(sqlite3 *db, nni_msg *msg)
{
	sqlite3_stmt *stmt;
	char *        sql = "DELETE FROM " table_msg " WHERE data = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	size_t   len  = 0;
	uint8_t *blob = nni_msg_serialize(msg, &len);
	sqlite3_bind_blob64(stmt, 1, blob, len, SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	nng_free(blob, len);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_all_msg(sqlite3 *db)
{
	char *sql = "UPDATE " table_main " SET m_id = 0 WHERE m_id > 0;"
	            "DELETE FROM " table_msg " WHERE id > 0;";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_exec(db, sql, 0, 0, NULL);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_check_remove_msg(sqlite3 *db, nni_msg *msg)
{
	sqlite3_stmt *stmt;
	// remove the msg if it was not referenced by table `t_main`
	char sql[] = "DELETE FROM " table_msg " AS msg WHERE "
	             "( SELECT COUNT(main.id) FROM " table_main " AS main  "
	             "WHERE  m_id = "
	             "( SELECT msg.id FROM t_msg "
	             "AS msg WHERE data = ? )) = 0 AND msg.data = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	size_t   len  = 0;
	uint8_t *blob = nni_msg_serialize(msg, &len);
	sqlite3_bind_blob64(stmt, 1, blob, len, SQLITE_TRANSIENT);
	sqlite3_bind_blob64(stmt, 2, blob, len, SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	nng_free(blob, len);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_unused_msg(sqlite3 *db)
{
	sqlite3_stmt *stmt;
	// remove the msg if it was not referenced by table `t_main`
	char sql[] = { "DELETE FROM " table_msg
		       " WHERE NOT EXISTS (SELECT 1 FROM " table_main
		       " WHERE m_id = " table_msg ".id AND m_id > 0)" };

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_oldest_and_unused(sqlite3 *db, uint64_t limit)
{
	char sql[512] = { 0 };
	int  rc;

	snprintf(sql, 512,
	    "DELETE FROM " table_main
	    " WHERE id NOT IN ( SELECT id FROM " table_main " ORDER BY"
	    " ts DESC LIMIT %llu);"
	    "DELETE FROM " table_msg
	    " WHERE NOT EXISTS (SELECT 1 FROM " table_main
	    " WHERE m_id = " table_msg ".id AND m_id > 0);",
	    (unsigned long long) limit);

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);

	rc = sqlite3_exec(db, sql, 0, 0, 0);

	if (rc != SQLITE_OK) {
		log_error("Failed execute sql to remove oldest and unused: %s",
		    sqlite3_errmsg(db));
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		return;
	}

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_set(
    sqlite3 *db, uint32_t pipe_id, uint16_t packet_id, nni_msg *msg)
{
	uint8_t  qos = MQTT_DB_GET_QOS_BITS(msg);
	nni_msg *m   = MQTT_DB_GET_MSG_POINTER(msg);
	set_main(db, pipe_id, packet_id, qos, m);
}

static void
set_main(sqlite3 *db, uint32_t pipe_id, uint16_t packet_id, uint8_t qos,
    nni_msg *msg)
{
	int64_t p_id = get_id_by_pipe(db, pipe_id);
	if (p_id == 0) {
		// can not find client
		return;
	}
	int64_t msg_id = get_id_by_msg(db, msg);
	if (msg_id == 0) {
		msg_id = insert_msg(db, msg);
	}
	uint8_t main_qos  = 0;
	int64_t main_m_id = 0;
	int64_t main_id =
	    get_id_by_p_id(db, p_id, packet_id, &main_qos, &main_m_id);
	if (main_id == 0) {
		insert_main(db, p_id, packet_id, qos, msg_id);
	} else {
		if (main_qos != qos || main_m_id != msg_id) {
			update_main(db, p_id, packet_id, qos, msg_id);
		}
	}
}

nni_msg *
nni_mqtt_qos_db_get(sqlite3 *db, uint32_t pipe_id, uint16_t packet_id)
{
	nni_msg *     msg = NULL;
	uint8_t       qos = 0;
	sqlite3_stmt *stmt;

	char sql[] =
	    "SELECT main.qos, msg.data FROM " table_pipe_client ""
	    " AS pipe JOIN "
	    "" table_main " AS main ON  main.p_id = pipe.id JOIN " table_msg ""
	    " AS msg ON  main.m_id = msg.id "
	    "WHERE pipe.pipe_id = ? AND main.packet_id = ?";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	if (SQLITE_ROW == sqlite3_step(stmt)) {
		qos            = sqlite3_column_int(stmt, 0);
		size_t   nbyte = (size_t) sqlite3_column_bytes16(stmt, 1);
		uint8_t *bytes = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 1), nbyte);

		// deserialize blob data to nni_msg
		msg = nni_msg_deserialize(bytes, nbyte);
		msg = MQTT_DB_PACKED_MSG_QOS(msg, qos);
		sqlite3_free(bytes);
	}
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return msg;
}

nni_msg *
nni_mqtt_qos_db_get_one(sqlite3 *db, uint32_t pipe_id, uint16_t *packet_id)
{
	nni_msg *     msg = NULL;
	uint8_t       qos = 0;
	sqlite3_stmt *stmt;

	char sql[] =
	    "SELECT main.packet_id, main.qos, msg.data FROM " table_pipe_client
	    " AS pipe JOIN "
	    "" table_main " AS main ON  main.p_id = pipe.id JOIN " table_msg ""
	    " AS msg ON "
	    " main.m_id = msg.id WHERE pipe.pipe_id = ? AND main.m_id > 0 "
	    "ORDER BY main.id LIMIT 1";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	// pipe ids exceed INT32_MAX; a 32-bit bind turns them negative and
	// misses the row written by the 64-bit binds in set_pipe
	sqlite3_bind_int64(stmt, 1, pipe_id);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		*packet_id     = sqlite3_column_int64(stmt, 0);
		qos            = sqlite3_column_int(stmt, 1);
		size_t   nbyte = (size_t) sqlite3_column_bytes16(stmt, 2);
		uint8_t *bytes = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 2), nbyte);
		// deserialize blob data to nni_msg
		msg = nni_msg_deserialize(bytes, nbyte);
		msg = MQTT_DB_PACKED_MSG_QOS(msg, qos);
		sqlite3_free(bytes);
	}
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return msg;
}

void
nni_mqtt_qos_db_remove(sqlite3 *db, uint32_t pipe_id, uint16_t packet_id)
{
	sqlite3_stmt *stmt;
	char *sql = "DELETE FROM " table_main " AS main WHERE main.p_id = "
	            "(SELECT pipe.id FROM " table_pipe_client ""
	            " AS pipe where  pipe.pipe_id = ? AND packet_id = ?)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_by_pipe(sqlite3 *db, uint32_t pipe_id)
{
	sqlite3_stmt *stmt;
	char *sql = "DELETE FROM " table_main " AS main WHERE main.p_id = "
	            "(SELECT pipe.id FROM " table_pipe_client ""
	            " AS pipe where  pipe.pipe_id = ?)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_foreach(sqlite3 *db, nni_idhash_cb cb)
{
	sqlite3_stmt *stmt;
	char          sql[] =
	    "SELECT pipe.pipe_id, msg.data FROM " table_main " AS main JOIN "
	    " " table_msg
	    " AS msg ON main.m_id = msg.id JOIN " table_pipe_client " "
	    " AS pipe ON main.p_id = pipe.id";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	while (SQLITE_ROW == sqlite3_step(stmt)) {
		uint32_t pipe_id = sqlite3_column_int64(stmt, 0);
		size_t   nbyte   = (size_t) sqlite3_column_bytes16(stmt, 1);
		uint8_t *bytes   = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 1), nbyte);
		// deserialize blob data to nni_msg
		nni_msg *msg = nni_msg_deserialize(bytes, nbyte);
		cb(&pipe_id, msg);
		if (msg) {
			nni_msg_free(msg);
		}
		sqlite3_free(bytes);
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

// Batching belongs to exactly one database — the broker's.  Checking the
// handle as well as the flag keeps a client or bridge database from taking
// the buffered path (and from repointing the flush at itself).
static bool
retain_batch_enabled(sqlite3 *db)
{
	return (g_retain.on && g_retain.db == db);
}

static void
retain_op_free(nni_retain_op *op)
{
	if (op->blob != NULL) {
		nng_free(op->blob, op->len);
	}
	if (op->topic != NULL) {
		nng_strfree(op->topic);
	}
	nng_free(op, sizeof(*op));
}

// Re-read the live batching parameters.  The broker rewrites these fields in
// place on a config reload, so reading them here is what lets a reload change
// the cadence without a restart.  Turning batching off at runtime is not
// supported -- the flush thread and the buffer were created at setup -- so a
// threshold of 0 keeps the last effective value and warns once.  Caller holds
// g_retain.lk.
static void
retain_batch_refresh(void)
{
	size_t   threshold;
	uint64_t interval;

	if (g_retain.conf == NULL) {
		return;
	}
	threshold = g_retain.conf->retain_flush_threshold;
	interval  = g_retain.conf->flush_interval;

	if (threshold == 0) {
		if (!g_retain.warned_off) {
			log_warn("retain batch: retain_flush_threshold=0 cannot "
			         "disable batching at runtime; keeping %zu until "
			         "restart",
			    g_retain.threshold);
			g_retain.warned_off = true;
		}
	} else {
		g_retain.warned_off = false;
		g_retain.threshold  = threshold;
		g_retain.hard_limit = threshold * 8;
		if (g_retain.hard_limit < 1024) {
			g_retain.hard_limit = 1024;
		}
	}
	if (interval != 0) {
		g_retain.interval_ms = interval;
	}
}

// Stop the flush thread and commit whatever it was still holding.  Shared by
// nni_mqtt_qos_db_close() and the exit hook, so both drain identically.
static void
retain_batch_shutdown(void)
{
	nni_mtx_lock(&g_retain.lk);
	// Drain while the buffer is still authoritative, then switch new work to
	// the synchronous path, both under one hold.  In that order the buffer
	// is already empty when `on` clears, so a write that goes synchronous
	// afterwards cannot be overwritten by a stale buffered value.
	retain_batch_flush();
	g_retain.on   = false;
	g_retain.stop = true;
	nni_cv_wake(&g_retain.cv);
	nni_mtx_unlock(&g_retain.lk);
	nni_thr_fini(&g_retain.thr);
	// The flush thread has exited and every other caller reaches the flush
	// only through the lock after re-checking `on`, which is now false, so
	// the dedicated connection is idle and can be closed.
	sqlite3_close(g_retain.flush_db);
	g_retain.flush_db = NULL;
	// g_retain.lk, .cv and .ops are deliberately left alive: a caller that
	// passed the unlocked `on` check just before it cleared may still be
	// about to take the lock, and would otherwise lock a finalized mutex or
	// dereference a NULL ops.  The post-lock recheck in each entry point
	// sends such a caller down the synchronous path.
}

#if defined(NNG_TEST_LIB)
// Run the real shutdown (drain, stop, join) without closing the database, so
// a test can drive callers that race the batcher going off.  This mirrors the
// broker's exit hook, which shuts down while the handle stays open.
void
nni_mqtt_qos_db_retain_batch_shutdown_for_test(void)
{
	if (g_retain.on) {
		retain_batch_shutdown();
	}
}

// Tear the process-global batcher down completely so the next unit test
// starts from a clean slate.  Production never does this: after a real
// shutdown the lock and buffer stay alive for callers that raced the flag
// going false (see retain_batch_shutdown()).  Tests are single-process and
// join their own workers first, so there is no such caller here.
void
nni_mqtt_qos_db_retain_batch_reset_for_test(void)
{
	khint_t k;

	if (g_retain.on) {
		retain_batch_shutdown();
	}
	if (!g_retain.initialized) {
		return;
	}
	nni_mtx_lock(&g_retain.lk);
	for (k = kh_begin(g_retain.ops); k != kh_end(g_retain.ops); ++k) {
		if (kh_exist(g_retain.ops, k)) {
			retain_op_free(kh_value(g_retain.ops, k));
		}
	}
	nni_mtx_unlock(&g_retain.lk);
	kh_destroy(retain_ops, g_retain.ops);
	nni_cv_fini(&g_retain.cv);
	nni_mtx_fini(&g_retain.lk);
	memset(&g_retain, 0, sizeof(g_retain));
}
#endif

// The broker never closes its database connection: its shutdown path returns
// from main and relies on atexit, and the socket fini that would call
// nni_mqtt_qos_db_close() is never reached.  Without this hook a batch still
// pending at exit would be dropped, where before this change every retained
// write was already committed and SQLite replayed it from the WAL.  It runs
// before nng's own exit handler, which was registered earlier and therefore
// runs later, so the database and the platform are still usable here.
static void
retain_batch_atexit(void)
{
	if (g_retain.on) {
		retain_batch_shutdown();
	}
}

// Commit the pending batch in a single transaction.  Caller holds g_retain.lk.
static void
retain_batch_flush(void)
{
	sqlite3      *db = g_retain.flush_db;
	sqlite3_stmt *ins = NULL, *del = NULL;
	char          ins_sql[] = "INSERT OR REPLACE INTO " table_retain
	             " ( topic, msg, proto_ver ) VALUES (?, ?, ?)";
	char          del_sql[] = "DELETE FROM " table_retain
	             " WHERE topic = ?";
	khint_t k;

	// Deliberately does not test g_retain.on: the shutdown path clears the
	// flag first (so new work goes synchronous) and still has to drain.
	if (db == NULL || g_retain.ops == NULL || g_retain.count == 0) {
		return;
	}

	if (sqlite3_exec(db, "BEGIN;", 0, 0, 0) != SQLITE_OK) {
		// The broker's connection holds the write lock, or is mid
		// transaction; retry the whole batch next tick.  Never run the
		// statements without our own transaction: they would join
		// whatever transaction is already open.
		log_error("retain flush: begin failed: %s", sqlite3_errmsg(db));
		return;
	}
	if (sqlite3_prepare_v2(db, ins_sql, strlen(ins_sql), &ins, 0) !=
	        SQLITE_OK ||
	    sqlite3_prepare_v2(db, del_sql, strlen(del_sql), &del, 0) !=
	        SQLITE_OK) {
		log_error("retain flush: prepare failed: %s",
		    sqlite3_errmsg(db));
		sqlite3_finalize(ins);
		sqlite3_finalize(del);
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		// keep the batch for the next tick rather than dropping it
		return;
	}

	for (k = kh_begin(g_retain.ops); k != kh_end(g_retain.ops); ++k) {
		nni_retain_op *op;

		if (!kh_exist(g_retain.ops, k)) {
			continue;
		}
		op = kh_value(g_retain.ops, k);
		if (op->blob == NULL) {
			// tombstone: drop a row left behind by an earlier flush
			sqlite3_reset(del);
			sqlite3_bind_text(del, 1, op->topic,
			    strlen(op->topic), SQLITE_TRANSIENT);
			sqlite3_step(del);
			sqlite3_reset(del);
			continue;
		}
		sqlite3_reset(ins);
		sqlite3_bind_text(ins, 1, op->topic, strlen(op->topic),
		    SQLITE_TRANSIENT);
		sqlite3_bind_blob64(
		    ins, 2, op->blob, op->len, SQLITE_TRANSIENT);
		sqlite3_bind_int(ins, 3, op->proto_ver);
		sqlite3_step(ins);
		sqlite3_reset(ins);
	}

	sqlite3_finalize(ins);
	sqlite3_finalize(del);
	if (sqlite3_exec(db, "COMMIT;", 0, 0, 0) != SQLITE_OK) {
		// Losing the commit loses the whole batch, not one message, so
		// keep it for the next tick rather than dropping it.
		log_error("retain flush: commit failed: %s",
		    sqlite3_errmsg(db));
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		return;
	}

	// the batch is now accounted for; release it
	for (k = kh_begin(g_retain.ops); k != kh_end(g_retain.ops); ++k) {
		if (kh_exist(g_retain.ops, k)) {
			retain_op_free(kh_value(g_retain.ops, k));
		}
	}
	kh_clear(retain_ops, g_retain.ops);
	g_retain.count = 0;
}

static void
retain_batch_thread(void *arg)
{
	NNI_ARG_UNUSED(arg);

	nni_mtx_lock(&g_retain.lk);
	while (!g_retain.stop) {
		nni_time deadline;

		// Pick up any reloaded threshold/interval before each wait.
		retain_batch_refresh();
		deadline = nni_clock() + g_retain.interval_ms;
		while (!g_retain.stop &&
		    g_retain.count < g_retain.threshold) {
			if (nni_cv_until(&g_retain.cv, deadline) ==
			    NNG_ETIMEDOUT) {
				break;
			}
		}
		retain_batch_flush();
	}
	retain_batch_flush();
	nni_mtx_unlock(&g_retain.lk);
}

// Buffer one retained store.  The message is serialized here, on the calling
// thread, so the buffer never shares an nni_msg with the publishing path.
static int
retain_batch_enqueue(const char *topic, nni_msg *msg, uint8_t proto_ver)
{
	size_t         len  = 0;
	uint8_t       *blob = nni_msg_serialize(msg, &len);
	nni_retain_op *op;
	khint_t        k;
	bool           wake;

	if (blob == NULL) {
		printf("nni_mqtt_msg_serialize failed\n");
		return (-1);
	}

	nni_mtx_lock(&g_retain.lk);
	if (!g_retain.on) {
		// Batching was switched off after the caller's unlocked check.
		nni_mtx_unlock(&g_retain.lk);
		nng_free(blob, len);
		return (RETAIN_BATCH_SYNC);
	}
	retain_batch_refresh();

	k = kh_get(retain_ops, g_retain.ops, topic);
	if (k != kh_end(g_retain.ops)) {
		// supersede whatever was pending for this topic
		op = kh_value(g_retain.ops, k);
		if (op->blob != NULL) {
			nng_free(op->blob, op->len);
		}
		op->blob      = blob;
		op->len       = len;
		op->proto_ver = proto_ver;
	} else {
		int absent = 0;

		op = nng_zalloc(sizeof(*op));
		if (op != NULL) {
			op->topic = nng_strdup(topic);
		}
		if (op == NULL || op->topic == NULL) {
			if (op != NULL) {
				nng_free(op, sizeof(*op));
			}
			nng_free(blob, len);
			nni_mtx_unlock(&g_retain.lk);
			// Could not buffer it; write it synchronously instead.
			return (RETAIN_BATCH_SYNC);
		}
		op->blob      = blob;
		op->len       = len;
		op->proto_ver = proto_ver;

		k = kh_put(
		    retain_ops, g_retain.ops, op->topic, &absent);
		if (absent != 1 || k == kh_end(g_retain.ops)) {
			retain_op_free(op);
			nni_mtx_unlock(&g_retain.lk);
			// Could not buffer it; write it synchronously
			// instead.
			return (RETAIN_BATCH_SYNC);
		}
		kh_value(g_retain.ops, k) = op;
		g_retain.count++;
	}

	wake = (g_retain.count >= g_retain.threshold);
	if (g_retain.count >= g_retain.hard_limit) {
		// the flush thread is not keeping up: bound the memory here
		retain_batch_flush();
	}
	nni_mtx_unlock(&g_retain.lk);

	if (wake) {
		nni_cv_wake(&g_retain.cv);
	}
	return (0);
}

static int
retain_batch_remove(const char *topic)
{
	nni_retain_op *op;
	khint_t        k;
	int            absent = 0;

	nni_mtx_lock(&g_retain.lk);
	if (!g_retain.on) {
		// Batching was switched off after the caller's unlocked check.
		nni_mtx_unlock(&g_retain.lk);
		return (RETAIN_BATCH_SYNC);
	}
	retain_batch_refresh();

	k = kh_get(retain_ops, g_retain.ops, topic);
	if (k != kh_end(g_retain.ops)) {
		op = kh_value(g_retain.ops, k);
		// The removal is now the last operation for this topic, so its
		// payload goes away and a tombstone takes its place.  Do not
		// drop the pair outright: an earlier value for this topic may
		// already have been flushed and still have a row, and the
		// DELETE is the only thing that removes it.
		if (op->blob != NULL) {
			nng_free(op->blob, op->len);
			op->blob      = NULL;
			op->len       = 0;
			op->proto_ver = 0;
		}
		nni_mtx_unlock(&g_retain.lk);
		return (0);
	}

	// Nothing buffered, but a row from an earlier flush may still exist,
	// so record the removal.
	op = nng_zalloc(sizeof(*op));
	if (op != NULL) {
		op->topic = nng_strdup(topic);
	}
	if (op == NULL || op->topic == NULL) {
		if (op != NULL) {
			nng_free(op, sizeof(*op));
		}
		nni_mtx_unlock(&g_retain.lk);
		// Could not record the tombstone; let the caller delete
		// synchronously rather than dropping the removal.
		return (RETAIN_BATCH_SYNC);
	}
	op->blob      = NULL;
	op->len       = 0;
	op->proto_ver = 0;

	k = kh_put(retain_ops, g_retain.ops, op->topic, &absent);
	if (absent != 1 || k == kh_end(g_retain.ops)) {
		retain_op_free(op);
		nni_mtx_unlock(&g_retain.lk);
		// Could not record the tombstone; let the caller delete
		// synchronously rather than dropping the removal.
		return (RETAIN_BATCH_SYNC);
	}
	kh_value(g_retain.ops, k) = op;
	g_retain.count++;
	nni_mtx_unlock(&g_retain.lk);
	return (0);
}

// Decode a stored or buffered retained message.
static nni_msg *
retain_msg_from_blob(uint8_t *blob, size_t len, uint8_t proto_ver)
{
	nni_msg *msg = nni_msg_deserialize(blob, len);

	if (msg == NULL) {
		return (NULL);
	}
	nni_mqtt_msg_proto_data_alloc(msg);
	if (proto_ver == MQTT_PROTOCOL_VERSION_v5) {
		nni_mqttv5_msg_decode(msg);
	} else {
		nni_mqtt_msg_decode(msg);
	}
	nni_mqtt_msg_set_publish_proto_version(msg, proto_ver);
	return (msg);
}

// Turn on write-behind batching for the retained-message store.  A threshold
// of 0 leaves the synchronous path in place, which is what every non-broker
// user of this database (and the unit tests) gets.  The threshold and
// interval are taken from `conf` and re-read from it on every flush tick, so
// a broker config reload that rewrites those fields takes effect without a
// restart.
void
nni_mqtt_qos_db_retain_batch_setup(sqlite3 *db, conf_sqlite *conf)
{
	size_t   threshold;
	uint64_t interval_ms;

	if (conf == NULL || conf->retain_flush_threshold == 0) {
		return;
	}
	if (g_retain.initialized) {
		// The lock and the buffer are never freed, so the batcher is
		// a one-shot: it cannot be re-armed on a second database, or
		// after a shutdown, without a restart.
		if (!g_retain.on) {
			log_warn("retain batch: already retired; batching "
			         "stays off for %p until restart",
			    (void *) db);
		} else if (g_retain.db != db) {
			log_warn("retain batcher already serves another "
			         "database: batching stays off for %p",
			    (void *) db);
		}
		return;
	}

	threshold   = conf->retain_flush_threshold;
	interval_ms = conf->flush_interval;

	g_retain.db          = db;
	g_retain.conf        = conf;
	g_retain.threshold   = threshold;
	g_retain.interval_ms = interval_ms == 0 ? 100 : interval_ms;
	g_retain.hard_limit  = threshold * 8;
	if (g_retain.hard_limit < 1024) {
		g_retain.hard_limit = 1024;
	}
	g_retain.stop  = false;
	g_retain.count = 0;

	// The flush thread writes on its own connection.  Sharing the broker's
	// handle would mean the flush's BEGIN..COMMIT could interleave with a
	// transaction another thread opened on that same handle, and one
	// thread's COMMIT or ROLLBACK would then commit or undo the other's
	// work.  A second connection to the same file is mediated by SQLite's
	// own locking instead.  The buffer lock still serializes every access
	// to the buffer itself.
	{
		const char *path = sqlite3_db_filename(db, "main");

		if (path == NULL ||
		    sqlite3_open(path, &g_retain.flush_db) != SQLITE_OK) {
			log_error("retain batch: cannot open a flush connection "
			          "for %s; batching stays off",
			    path == NULL ? "?" : path);
			sqlite3_close(g_retain.flush_db);
			g_retain.flush_db = NULL;
			return;
		}
		set_db_pragma(g_retain.flush_db);
		// Wait rather than fail when the broker's connection is mid-write.
		sqlite3_busy_timeout(g_retain.flush_db, 5000);
	}

	nni_mtx_init(&g_retain.lk);
	nni_cv_init(&g_retain.cv, &g_retain.lk);

	g_retain.ops = kh_init(retain_ops);
	if (g_retain.ops == NULL) {
		nni_cv_fini(&g_retain.cv);
		nni_mtx_fini(&g_retain.lk);
		return;
	}
	if (nni_thr_init(&g_retain.thr, retain_batch_thread, NULL) != 0) {
		kh_destroy(retain_ops, g_retain.ops);
		g_retain.ops = NULL;
		nni_cv_fini(&g_retain.cv);
		nni_mtx_fini(&g_retain.lk);
		return;
	}
	g_retain.initialized = true;
	g_retain.on          = true;
	nni_thr_run(&g_retain.thr);

	// Drains the batch if the process exits without closing the database.
	// Idempotent: the handler is a no-op once the buffer has been released.
	(void) atexit(retain_batch_atexit);
}

int
nni_mqtt_qos_db_set_retain(
    sqlite3 *db, const char *topic, nni_msg *msg, uint8_t proto_ver)
{
	if (retain_batch_enabled(db)) {
		int rv = retain_batch_enqueue(topic, msg, proto_ver);

		if (rv != RETAIN_BATCH_SYNC) {
			return (rv);
		}
		// Batching was switched off under us; fall through to the
		// synchronous path.
	}

	char sql[] = "INSERT or REPLACE INTO " table_retain
	             " ( topic, msg, proto_ver ) VALUES (?, ?, ?)";
	size_t   len  = 0;
	uint8_t *blob = nni_msg_serialize(msg, &len);
	if (!blob) {
		printf("nni_mqtt_msg_serialize failed\n");
		return -1;
	}
	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_text(stmt, 1, topic, strlen(topic), SQLITE_TRANSIENT);
	sqlite3_bind_blob64(stmt, 2, blob, len, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, proto_ver);
	sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	nng_free(blob, len);

	return 0;
}

nni_msg *
nni_mqtt_qos_db_get_retain(sqlite3 *db, const char *topic)
{
	nni_msg *msg = NULL;
	char     sql[] =
	    "SELECT msg, proto_ver FROM " table_retain " WHERE topic = ? LIMIT 1";

	sqlite3_stmt *stmt;

	if (retain_batch_enabled(db)) {
		// The buffered store has not reached the database yet, so the
		// caller has to be served from the buffer to observe its own
		// write.  A pending removal answers NULL.
		nni_msg *buffered = NULL;
		bool     found    = false;
		khint_t  k;

		nni_mtx_lock(&g_retain.lk);
		if (g_retain.on) {
			k = kh_get(retain_ops, g_retain.ops, topic);
			if (k != kh_end(g_retain.ops)) {
				nni_retain_op *op = kh_value(g_retain.ops, k);
				found = true;
				if (op->blob != NULL) {
					buffered = retain_msg_from_blob(
					    op->blob, op->len, op->proto_ver);
				}
			}
		}
		nni_mtx_unlock(&g_retain.lk);
		if (found) {
			return (buffered);
		}
	}

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_text(stmt, 1, topic, strlen(topic), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		size_t   nbyte     = (size_t) sqlite3_column_bytes16(stmt, 0);
		uint8_t *bytes     = sqlite3_malloc(nbyte);
		uint8_t  proto_ver = (uint8_t) sqlite3_column_int(stmt, 1);

		memcpy(bytes, sqlite3_column_blob(stmt, 0), nbyte);
		msg = retain_msg_from_blob(bytes, nbyte, proto_ver);
		sqlite3_free(bytes);
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return msg;
}

nni_msg **
nni_mqtt_qos_db_find_retain(sqlite3 *db, const char *topic_pattern)
{
	nni_msg * msg     = NULL;
	nni_msg **msg_vec = NULL;
	char **   expired_topics = NULL;

	if (retain_batch_enabled(db)) {
		// The query below matches with SQL GLOB, so anything still
		// buffered is invisible to it.  Push the batch out first or a
		// subscriber would miss a retained message published moments
		// ago.  This runs on SUBSCRIBE only, not on the hot path.
		nni_mtx_lock(&g_retain.lk);
		if (g_retain.on) {
			retain_batch_flush();
		}
		nni_mtx_unlock(&g_retain.lk);
	}

	char *topic_str = nng_strdup(topic_pattern);

	for (size_t i = 0; i < strlen(topic_pattern); i++) {
		if (topic_pattern[i] == '+' || topic_pattern[i] == '#') {
			topic_str[i] = '*';
		}
	}

	char sql[] = "SELECT msg, proto_ver FROM " table_retain " WHERE topic GLOB ?";

	sqlite3_stmt *stmt;

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);

	int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	if (rc != SQLITE_OK) {
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		nng_strfree(topic_str);
		return NULL;
	}

	rc = sqlite3_bind_text(stmt, 1, topic_str, strlen(topic_str), SQLITE_TRANSIENT);
	if (rc != SQLITE_OK) {
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		sqlite3_finalize(stmt);
		nng_strfree(topic_str);
		return NULL;
	}
	while (SQLITE_ROW == sqlite3_step(stmt)) {
		size_t      nbyte = (size_t) sqlite3_column_bytes16(stmt, 0);
		const void *blob  = sqlite3_column_blob(stmt, 0);
		if (nbyte > 0 && blob == NULL) {
			continue;
		}
		// uint8_t *bytes = sqlite3_malloc(nbyte);
		// if (bytes == NULL) {
		// 	continue;
		// }
		// memcpy(bytes, blob, nbyte);
		// msg = nni_msg_deserialize(bytes, nbyte);
		// sqlite3_free(bytes);
		msg = nni_msg_deserialize((uint8_t *) blob, nbyte);
        if (msg == NULL) {
            continue;
        }

		uint8_t proto_ver = sqlite3_column_int(stmt, 1);
		nni_mqtt_msg_proto_data_alloc(msg);
		if (proto_ver == MQTT_PROTOCOL_VERSION_v5) {
			nni_mqttv5_msg_decode(msg);
		} else {
			nni_mqtt_msg_decode(msg);
		}
		if (is_msg_expired(msg)) {
			uint32_t tlen = 0;
            const char *real_topic = nni_msg_get_pub_topic(msg, &tlen);
            if (real_topic != NULL && tlen > 0) {
                cvector_push_back(expired_topics, nng_strndup(real_topic, tlen));
            }
            nni_msg_free(msg);
        } else {
            nni_mqtt_msg_set_publish_proto_version(msg, proto_ver);
            cvector_push_back(msg_vec, msg);
        }

	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	for (size_t i = 0; i < cvector_size(expired_topics); i++) {
        nni_mqtt_qos_db_remove_retain(db, expired_topics[i]);
        nng_strfree(expired_topics[i]);
    }
    cvector_free(expired_topics);
	
	nng_strfree(topic_str);

	return msg_vec;
}

int
nni_mqtt_qos_db_remove_retain(sqlite3 *db, const char *topic)
{
	if (retain_batch_enabled(db) &&
	    retain_batch_remove(topic) != RETAIN_BATCH_SYNC) {
		return (0);
	}

	char sql[] = "DELETE FROM " table_retain "  WHERE topic = ?";

	sqlite3_stmt *stmt;

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_text(stmt, 1, topic, strlen(topic), SQLITE_TRANSIENT);
	sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	return sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

int
nni_mqtt_qos_db_set_client_msg(sqlite3 *db, uint32_t pipe_id,
    uint16_t packet_id, nni_msg *msg, const char *config_name, uint8_t proto_ver)
{
	char sql[] =
	    "INSERT INTO " table_client_msg
	    " ( pipe_id, packet_id, data, proto_ver, info_id ) "
	    " VALUES (?, ?, ?, ?, (SELECT id FROM " table_client_info
	    " WHERE config_name = ? LIMIT 1 ))";
	size_t   len  = 0;
	uint8_t *blob = nni_mqtt_msg_serialize(msg, &len, proto_ver);
	if (!blob) {
		printf("nni_mqtt_msg_serialize failed\n");
		return -1;
	}
	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	// 64-bit like the readers: pipe ids exceed INT32_MAX
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	sqlite3_bind_blob64(stmt, 3, blob, len, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, proto_ver);
	sqlite3_bind_text(
	    stmt, 5, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	nng_free(blob, len);
	int rv = sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	nni_msg_free(msg);
	return rv;
}

nni_msg *
nni_mqtt_qos_db_get_client_msg(
    sqlite3 *db, uint32_t pipe_id, uint16_t packet_id, const char *config_name)
{
	nni_msg *     msg = NULL;
	sqlite3_stmt *stmt;

	char sql[] =
	    "SELECT proto_ver, data FROM " table_client_msg ""
	    " WHERE pipe_id = ? AND packet_id = ? AND info_id = (SELECT id "
	    "FROM " table_client_info " WHERE config_name = ? LIMIT 1) ";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	sqlite3_bind_text(
	    stmt, 3, config_name, strlen(config_name), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		uint8_t  proto_ver = sqlite3_column_int(stmt, 0);
		size_t   nbyte     = (size_t) sqlite3_column_bytes16(stmt, 1);
		uint8_t *bytes     = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 1), nbyte);
		// deserialize blob data to nni_msg
		msg = nni_mqtt_msg_deserialize(
		    bytes, nbyte, pipe_id > 0 ? true : false, proto_ver);
		sqlite3_free(bytes);
	}
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return msg;
}

void
nni_mqtt_qos_db_remove_client_msg(
    sqlite3 *db, uint32_t pipe_id, uint16_t packet_id, const char *config_name)
{
	sqlite3_stmt *stmt;

	char sql[] =
	    "DELETE FROM " table_client_msg
	    " WHERE pipe_id = ? AND packet_id = ? AND info_id = (SELECT id "
	    "FROM "table_client_info" WHERE config_name = ? LIMIT 1)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	sqlite3_bind_text(
	    stmt, 3, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_client_msg_by_id(sqlite3 *db, uint64_t id)
{
	sqlite3_stmt *stmt;

	char sql[] = "DELETE FROM " table_client_msg " WHERE id = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, id);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_reset_client_msg_pipe_id(sqlite3 *db, const char *config_name)
{
	sqlite3_stmt *stmt;

	char sql[] =
	    "UPDATE " table_client_msg " SET pipe_id = 0 WHERE info_id = "
	    "(SELECT id FROM " table_client_info
	    " WHERE config_name = ? LIMIT 1)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

static void
remove_oldest_client_msg(sqlite3 *db, const char *table_name,
    const char *col_name, uint64_t limit, const char *config_name)
{
	sqlite3_stmt *stmt;
	char          sql[256] = { 0 };

	snprintf(sql, 256,
	    "DELETE FROM %s WHERE %s NOT IN ( SELECT %s FROM %s WHERE info_id "
	    "= (SELECT id "
	    "FROM " table_client_info
	    " WHERE config_name = ? LIMIT 1) ORDER BY"
	    " %s DESC LIMIT ?)",
	    table_name, col_name, col_name, table_name, col_name);

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, limit);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_oldest_client_msg(
    sqlite3 *db, uint64_t limit, const char *config_name)
{
	remove_oldest_client_msg(
	    db, table_client_msg, "ts", limit, config_name);
}

nni_msg *
nni_mqtt_qos_db_get_one_client_msg(
    sqlite3 *db, uint64_t *id, uint16_t *packet_id, const char *config_name)
{
	nni_msg *     msg = NULL;
	sqlite3_stmt *stmt;

	char sql[] =
	    "SELECT id, pipe_id, packet_id, data, proto_ver FROM " table_client_msg
	    " WHERE info_id = (SELECT id FROM " table_client_info
	    " WHERE config_name = ? LIMIT 1) "
	    " ORDER BY id LIMIT 1";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		*id              = (uint64_t) sqlite3_column_int64(stmt, 0);
		uint32_t pipe_id = sqlite3_column_int64(stmt, 1);
		*packet_id       = sqlite3_column_int(stmt, 2);
		size_t   nbyte   = (size_t) sqlite3_column_bytes16(stmt, 3);
		uint8_t *bytes   = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 3), nbyte);
		uint8_t proto_ver = sqlite3_column_int(stmt, 4);

		// deserialize blob data to nni_msg
		msg = nni_mqtt_msg_deserialize(
		    bytes, nbyte, pipe_id > 0 ? true : false, proto_ver);
		sqlite3_free(bytes);
	}
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return msg;
}

int
nni_mqtt_qos_db_set_client_offline_msg(
    sqlite3 *db, nni_msg *msg, const char *config_name, uint8_t proto_ver)
{
	char sql[] = "INSERT INTO " table_client_offline_msg
	             " (proto_ver, data, info_id ) "
	             "VALUES ( ?, ?, (SELECT id FROM " table_client_info
	             " WHERE config_name = ? LIMIT 1 ))";
	size_t   len  = 0;
	uint8_t *blob = nni_mqtt_msg_serialize(msg, &len, proto_ver);

	if (!blob) {
		printf("nni_mqtt_msg_serialize failed\n");
		nni_msg_free(msg);
		return -1;
	}

	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int(stmt, 1, proto_ver);
	sqlite3_bind_blob64(stmt, 2, blob, len, SQLITE_TRANSIENT);
	sqlite3_bind_text(
	    stmt, 3, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	nng_free(blob, len);
	int rv = sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	nni_msg_free(msg);
	return rv;
}

int
nni_mqtt_qos_db_set_client_offline_msg_batch(
    sqlite3 *db, nni_lmq *lmq, const char *config_name, uint8_t proto_ver)
{
	int info_id = get_client_info_id(db, config_name);
	if(info_id < 0) {
		return -1;
	}

	char sql[] =
	    "INSERT INTO " table_client_offline_msg " ( data, proto_ver, info_id ) "
	    "VALUES ( ? , ? , ?)";

	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	size_t lmq_len = nni_lmq_len(lmq);
	for (size_t i = 0; i < lmq_len; i++) {
		nni_msg *msg;
		if (nni_lmq_get(lmq, &msg) == 0) {
			size_t   len  = 0;
			uint8_t *blob = nni_mqtt_msg_serialize(msg, &len, proto_ver);
			if (!blob) {
				printf("nni_mqtt_msg_serialize failed\n");
				nni_msg_free(msg);
				continue;
			}
			sqlite3_reset(stmt);
			sqlite3_bind_blob64(
			    stmt, 1, blob, len, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, proto_ver);
			sqlite3_bind_int(stmt, 3, info_id);
			sqlite3_step(stmt);
			nng_free(blob, len);
			nni_msg_free(msg);
		}
	}
	sqlite3_finalize(stmt);
	int rv = sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return rv;
}

nng_msg *
nni_mqtt_qos_db_get_client_offline_msg(
    sqlite3 *db, int64_t *row_id, const char *config_name)
{
	nni_msg *     msg = NULL;
	sqlite3_stmt *stmt;

	char sql[] = "SELECT id, proto_ver, data FROM " table_client_offline_msg
	             " WHERE info_id = (SELECT id FROM " table_client_info
	             " WHERE config_name = ? LIMIT 1) "
	             " ORDER BY id ASC LIMIT 1 ";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		*row_id            = sqlite3_column_int64(stmt, 0);
		uint8_t  proto_ver = sqlite3_column_int(stmt, 1);
		size_t   nbyte     = (size_t) sqlite3_column_bytes16(stmt, 2);
		uint8_t *bytes     = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 2), nbyte);
		// deserialize blob data to nni_msg
		msg = nni_mqtt_msg_deserialize(bytes, nbyte, false, proto_ver);
		sqlite3_free(bytes);
	}
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return msg;
}

void
nni_mqtt_qos_db_remove_oldest_client_offline_msg(
    sqlite3 *db, uint64_t limit, const char *config_name)
{
	remove_oldest_client_msg(
	    db, table_client_offline_msg, "ts", limit, config_name);
}

int
nni_mqtt_qos_db_remove_client_offline_msg(sqlite3 *db, int64_t row_id)
{
	sqlite3_stmt *stmt;
	char sql[] = "DELETE FROM " table_client_offline_msg " WHERE id = ?";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_int64(stmt, 1, row_id);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

int
nni_mqtt_qos_db_remove_all_client_offline_msg(sqlite3 *db, const char *config_name)
{
	sqlite3_stmt *stmt;
	char          sql[] = "DELETE FROM " table_client_offline_msg
	             " WHERE info_id = (SELECT id FROM " table_client_info
	             " WHERE config_name = ? LIMIT 1)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

static int
get_client_info_id(sqlite3 *db, const char *config_name)
{
	int           id = -1;
	sqlite3_stmt *stmt;
	char          sql[] = "SELECT id FROM " table_client_info
	             " WHERE config_name = ? LIMIT 1";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);
	if (SQLITE_ROW == sqlite3_step(stmt)) {
		id = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	return id;
}

int
nni_mqtt_qos_db_set_client_info(sqlite3 *db, const char *config_name,
    const char *client_id, const char *proto_name, uint8_t proto_ver)
{
	char sql[] = "INSERT OR REPLACE INTO " table_client_info
	             " (id, config_name, client_id, proto_name, proto_ver ) "
	             " VALUES ( ( SELECT id FROM " table_client_info
	             " WHERE config_name = ? LIMIT 1 ), ?, ?, ?, ? )";

	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_bind_text(
	    stmt, 2, config_name, strlen(config_name), SQLITE_TRANSIENT);
	if (client_id) {
		sqlite3_bind_text(
		    stmt, 3, client_id, strlen(client_id), SQLITE_TRANSIENT);
	} else {
		sqlite3_bind_null(stmt, 3);
	}
	sqlite3_bind_text(
	    stmt, 4, proto_name, strlen(proto_name), SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 5, proto_ver);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	int rv = sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	return rv;
}

static uint8_t *
nni_mqtt_msg_serialize(nni_msg *msg, size_t *out_len, uint8_t proto_ver)
{
	NNI_ARG_UNUSED(proto_ver);

	size_t len = nni_msg_header_len(msg) + nni_msg_len(msg) +
	    (sizeof(uint32_t) * 2) + sizeof(nni_time) + sizeof(nni_aio *);
	*out_len = len;

	// bytes:
	// header:  header_len(uint32) + header(header_len)
	// body:	body_len(uint32) + body(body_len)
	// time:	nni_time(uint64)
	// aio:		address value
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
	if (write_uint64(nni_msg_get_timestamp(msg), &buf) != 0) {
		goto out;
	}

	nni_aio *aio = NULL;
	if ((aio = nni_mqtt_msg_get_aio(msg)) != NULL) {
		write_uint64((uint64_t) aio, &buf);
	} else {
		write_uint64((uint64_t) 0UL, &buf);
	}

	return bytes;

out:
	free(bytes);
	return NULL;
}

static nni_msg *
nni_mqtt_msg_deserialize(
    uint8_t *bytes, size_t len, bool aio_available, uint8_t proto_ver)
{
	nni_msg *msg;
	if (nni_mqtt_msg_alloc(&msg, 0) != 0) {
		return NULL;
	}

	struct pos_buf buf = { .curpos = &bytes[0], .endpos = &bytes[len] };

	// bytes:
	// header:  header_len(uint32) + header(header_len)
	// body:	body_len(uint32) + body(body_len)
	// time:	nni_time(uint64)
	// aio:		address value
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
	buf.curpos += body_len;

	nni_time ts = 0;
	if (read_uint64(&buf, &ts) != 0) {
		goto out;
	}
	nni_msg_set_timestamp(msg, ts);

	if (proto_ver == MQTT_PROTOCOL_VERSION_v5) {
		nni_mqttv5_msg_decode(msg);
	} else {
		nni_mqtt_msg_decode(msg);
	}

	if (aio_available) {
		uint64_t addr = 0;
		if (read_uint64(&buf, &addr) != 0) {
			goto out;
		}
		nni_mqtt_msg_set_aio(msg, (nni_aio *) addr);
	} else {
		nni_mqtt_msg_set_aio(msg, NULL);
	}

	return msg;

out:
	if (msg) {
		nni_msg_free(msg);
	}
	return NULL;
}

static uint8_t *
nni_msg_serialize(nni_msg *msg, size_t *out_len)
{
	size_t len = nni_msg_header_len(msg) + nni_msg_len(msg) +
	    (sizeof(uint32_t) * 2) + sizeof(nni_time) + sizeof(nni_aio *);
	*out_len = len;

	// bytes:
	// header:  header_len(uint32) + header(header_len)
	// body:	body_len(uint32) + body(body_len)
	// time:	nni_time(uint64)
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
	if (write_uint64(nni_msg_get_timestamp(msg), &buf) != 0) {
		goto out;
	}
	if (write_byte(nni_msg_cmd_type(msg), &buf)) {
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
	// time:	nni_time(uint64)
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
	buf.curpos += body_len;

	nni_time ts = 0;
	if (read_uint64(&buf, &ts) != 0) {
		goto out;
	}
	nni_msg_set_timestamp(msg, ts);

	uint8_t cmd_type = 0;
	if (read_byte(&buf, &cmd_type) == 0) {
		nni_msg_set_cmd_type(msg, cmd_type);
	}

	return msg;

out:
	nni_msg_free(msg);
	return NULL;
}

#ifdef NNG_SUPP_SQLITE
void
nni_mqtt_sqlite_db_init(nng_mqtt_sqlite_option *opt, const char *db_name)
{
	if (opt != NULL && opt->bridge != NULL &&
	    opt->sqlite_conf->enable) {
		nni_lmq_init(&opt->offline_cache,
		    opt->sqlite_conf->flush_mem_threshold);
		opt->db_name = nni_strdup(db_name);
		nni_mqtt_qos_db_init((sqlite3 **)&opt->db,
		    opt->sqlite_conf->mounted_file_path, db_name, false);
		nni_mqtt_qos_db_set_client_info(opt->db, opt->bridge_name,
		    NULL, "MQTT", opt->proto_ver);
	}
}

void
nni_mqtt_sqlite_db_fini(nni_mqtt_sqlite_option *sqlite_opt)
{
	if (sqlite_opt != NULL &&
	    sqlite_opt->sqlite_conf->enable) {
		nni_lmq_fini(&sqlite_opt->offline_cache);
		nni_strfree(sqlite_opt->db_name);
		nni_mqtt_qos_db_close(sqlite_opt->db);
	}
}
#endif
