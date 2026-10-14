#include "nng/nng.h"
#include "mqtt_qos_db.h"
#include "nng/supplemental/nanolib/log.h"
#include "core/nng_impl.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/supplemental/sqlite/sqlite3.h"
#include "nng/supplemental/nanolib/cvector.h"
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
       uint64_t limit, int info_id);
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
	             "  ts DATETIME DEFAULT CURRENT_TIMESTAMP );"
	             "CREATE INDEX IF NOT EXISTS idx_client_msg_lookup ON " table_client_msg " (info_id, pipe_id, packet_id);"
	             "CREATE INDEX IF NOT EXISTS idx_client_msg_evict ON " table_client_msg " (info_id, id DESC);";

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
	             "  ts DATETIME DEFAULT CURRENT_TIMESTAMP );"
	             "CREATE INDEX IF NOT EXISTS idx_offline_msg_evict ON " table_client_offline_msg " (info_id, id DESC);";

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

int
nni_mqtt_qos_db_set_retain(
    sqlite3 *db, const char *topic, nni_msg *msg, uint8_t proto_ver)
{
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

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);

	sqlite3_bind_text(stmt, 1, topic, strlen(topic), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		size_t   nbyte = (size_t) sqlite3_column_bytes16(stmt, 0);
		uint8_t *bytes = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 0), nbyte);
		msg = nni_msg_deserialize(bytes, nbyte);
		sqlite3_free(bytes);
		uint8_t proto_ver = sqlite3_column_int(stmt, 1);
		nni_mqtt_msg_proto_data_alloc(msg);
		// TODO: decode multiple times. performance tunning
		if (proto_ver == MQTT_PROTOCOL_VERSION_v5) {
			nni_mqttv5_msg_decode(msg);
		} else {
			nni_mqtt_msg_decode(msg);
		}
		nni_mqtt_msg_set_publish_proto_version(msg, proto_ver);
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
    uint16_t packet_id, nni_msg *msg, int info_id, uint8_t proto_ver)
{
	char sql[] =
	    "INSERT INTO " table_client_msg
	    " ( pipe_id, packet_id, data, proto_ver, info_id ) "
	    " VALUES (?, ?, ?, ?, ?)";
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
	
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	sqlite3_bind_blob64(stmt, 3, blob, len, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, proto_ver);
	sqlite3_bind_int(stmt, 5, info_id); // 新增：直接绑定 info_id
	
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
remove_oldest_client_msg(
    sqlite3 *db, const char *table_name, uint64_t limit, int info_id)
{
	sqlite3_stmt *stmt;
	char          sql[512] = { 0 };

	// 0 means "no cap": the shipped configs document it as
	// "( 0 means ineffective )". Without this, LIMIT 0 makes the subquery
	// empty and NOT IN () deletes the whole table.
	if (limit == 0) {
		return;
	}

	snprintf(sql, sizeof(sql),
	    "DELETE FROM %s WHERE info_id = ?1 AND id NOT IN (SELECT id FROM %s"
	    " WHERE info_id = ?1 ORDER BY id DESC LIMIT ?2)",
	    table_name, table_name);

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	
	// 直接绑定整型变量
	sqlite3_bind_int(stmt, 1, info_id);
	sqlite3_bind_int64(stmt, 2, limit);
	
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);
}

void
nni_mqtt_qos_db_remove_oldest_client_msg(
    sqlite3 *db, uint64_t limit, int info_id)
{
	remove_oldest_client_msg(
	    db, table_client_msg, limit, info_id);
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
    sqlite3 *db, uint64_t limit, int info_id)
{
	remove_oldest_client_msg(
	    db, table_client_offline_msg, limit, info_id);
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
		opt->info_id = get_client_info_id(opt->db, opt->bridge_name);
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

// Format version of a per-node Bridge Cache database, kept in the fixed
// SQLite pragma `user_version` (it has no renameable form). 0 means the one
// time move out of the pre-per-node shared database has not finished yet.
#define BRIDGE_CACHE_DB_VERSION_MIGRATED 1

// Copy this Bridge Node's rows out of the pre-per-node shared database
// (mounted_file_path/mqtt_client.db, where every bridge used to write) into its
// own file, then drop them from the legacy file. Rows of other bridges stay
// behind. Best effort: a failure must not stop the broker.
void
nni_mqtt_qos_db_migrate_legacy(sqlite3 *db, const char *legacy_dir,
    const char *legacy_name, const char *config_name)
{
	char          legacy_path[1024] = { 0 };
	sqlite3_stmt *stmt              = NULL;
	int           ftype             = 0;
	int           rc;
	int           moved = 0;
	int64_t       info_id;

	// A NULL directory means the working directory, the same way
	// nni_mqtt_sqlite_db_init() reads it, so a deployment that never set
	// mounted_file_path still finds its legacy file.
	if (db == NULL || config_name == NULL || legacy_name == NULL) {
		return;
	}
	if (get_db_path(legacy_path, legacy_dir, legacy_name) == NULL ||
	    nni_file_type(legacy_path, &ftype) != 0) {
		log_debug("Bridge Cache: no legacy database at %s, skipping "
		          "migration",
		    legacy_path);
		return;
	}

	info_id = get_client_info_id(db, config_name);
	if (info_id <= 0) {
		log_error("Bridge Cache: cannot migrate '%s', its client_info "
		          "row is missing",
		    config_name);
		return;
	}

	// ATTACH cannot run inside a transaction.
	if (sqlite3_prepare_v2(
	        db, "ATTACH DATABASE ? AS legacy", -1, &stmt, NULL) != SQLITE_OK) {
		log_error("Bridge Cache: cannot prepare ATTACH: %s",
		    sqlite3_errmsg(db));
		return;
	}
	sqlite3_bind_text(stmt, 1, legacy_path, -1, SQLITE_TRANSIENT);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		log_error("Bridge Cache: cannot attach %s: %s", legacy_path,
		    sqlite3_errmsg(db));
		return;
	}

	// The legacy file is the only database shared between bridges on this
	// path, so wait for its lock rather than failing at once.
	sqlite3_busy_timeout(db, 5000);

	// Step 1: copy the rows and record completion, both in the node database.
	// That transaction writes one database, so its commit is atomic on its
	// own. Copying and deleting in a single transaction would not be: SQLite
	// keeps WAL databases out of the super-journal, so a crash during such a
	// commit could drop the legacy rows without keeping the copy.
	if (sqlite3_exec(db, "BEGIN IMMEDIATE;", 0, 0, 0) != SQLITE_OK) {
		log_error("Bridge Cache: cannot begin the migration for '%s': %s",
		    config_name, sqlite3_errmsg(db));
		sqlite3_exec(db, "DETACH DATABASE legacy;", 0, 0, 0);
		return;
	}
	rc = sqlite3_prepare_v2(db,
	    "INSERT INTO " table_client_offline_msg
	    " (data, info_id, proto_ver, ts) SELECT m.data, ?1, m.proto_ver, "
	    "m.ts FROM legacy." table_client_offline_msg
	    " m WHERE m.info_id = (SELECT id FROM legacy." table_client_info
	    " WHERE config_name = ?2 LIMIT 1)",
	    -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, info_id);
		sqlite3_bind_text(stmt, 2, config_name, -1, SQLITE_TRANSIENT);
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
	if (rc != SQLITE_DONE) {
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		log_error("Bridge Cache: copying the cached messages of '%s' out "
		          "of %s failed: %s",
		    config_name, legacy_path, sqlite3_errmsg(db));
		sqlite3_exec(db, "DETACH DATABASE legacy;", 0, 0, 0);
		return;
	}
	moved = sqlite3_changes(db);

	char pragma[64];
	// Committed with the copy, so "the rows moved" and "this file is marked
	// as migrated" cannot be observed apart. A marker that cannot be written
	// aborts the step instead of leaving an unmarked copy behind, which the
	// next start would copy a second time.
	snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d;",
	    BRIDGE_CACHE_DB_VERSION_MIGRATED);
	if (sqlite3_exec(db, pragma, 0, 0, 0) != SQLITE_OK) {
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		log_error("Bridge Cache: cannot record the migration of '%s': %s",
		    config_name, sqlite3_errmsg(db));
		sqlite3_exec(db, "DETACH DATABASE legacy;", 0, 0, 0);
		return;
	}
	if (sqlite3_exec(db, "COMMIT;", 0, 0, 0) != SQLITE_OK) {
		sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
		log_error("Bridge Cache: committing the copy for '%s' failed: %s",
		    config_name, sqlite3_errmsg(db));
		sqlite3_exec(db, "DETACH DATABASE legacy;", 0, 0, 0);
		return;
	}
	if (moved > 0) {
		log_info("Bridge Cache: migrated %d cached message(s) for '%s' "
		         "from %s",
		    moved, config_name, legacy_path);
	} else {
		log_debug("Bridge Cache: nothing to migrate for '%s'", config_name);
	}

	// Step 2: drop the copied rows from the legacy file. The node database
	// already holds them and says the migration is done, so a failure here
	// leaves an extra copy behind rather than losing anything. It also means
	// the cleanup is not attempted again: the marker skips this whole path
	// on the next start.
	if (sqlite3_exec(db, "BEGIN IMMEDIATE;", 0, 0, 0) == SQLITE_OK) {
		rc = sqlite3_prepare_v2(db,
		    "DELETE FROM legacy." table_client_offline_msg
		    " WHERE info_id = (SELECT id FROM legacy." table_client_info
		    " WHERE config_name = ?1 LIMIT 1)",
		    -1, &stmt, NULL);
		if (rc == SQLITE_OK) {
			sqlite3_bind_text(
			    stmt, 1, config_name, -1, SQLITE_TRANSIENT);
			rc = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
		}
		if (rc == SQLITE_DONE &&
		    sqlite3_exec(db, "COMMIT;", 0, 0, 0) == SQLITE_OK) {
			// done
		} else {
			sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
			log_error("Bridge Cache: could not remove the copied "
			          "messages of '%s' from %s, they stay there as a "
			          "duplicate: %s",
			    config_name, legacy_path, sqlite3_errmsg(db));
		}
	} else {
		log_error("Bridge Cache: cannot clean up %s after copying '%s': %s",
		    legacy_path, config_name, sqlite3_errmsg(db));
	}
	sqlite3_exec(db, "DETACH DATABASE legacy;", 0, 0, 0);
}

static bool
bridge_cache_migration_done(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	bool          done = false;

	if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL) !=
	    SQLITE_OK) {
		return false;
	}
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		done = (sqlite3_column_int(stmt, 0) >=
		    BRIDGE_CACHE_DB_VERSION_MIGRATED);
	}
	sqlite3_finalize(stmt);
	return done;
}

void
nni_mqtt_sqlite_db_init_legacy(nng_mqtt_sqlite_option *opt,
    const char *db_name, const char *legacy_dir, const char *legacy_name)
{
	nni_mqtt_sqlite_db_init(opt, db_name);

	if (opt == NULL || opt->db == NULL) {
		return;
	}
	// The per-node file is created by the init above, so "is the file new"
	// cannot tell a first start from a start that died before the move
	// finished. The node database records completion instead: a failed or
	// interrupted move leaves the marker unset and is retried on the next
	// start, while a finished one is skipped.
	if (bridge_cache_migration_done(opt->db)) {
		return;
	}
	nni_mqtt_qos_db_migrate_legacy(
	    opt->db, legacy_dir, legacy_name, opt->bridge_name);
}
#endif
