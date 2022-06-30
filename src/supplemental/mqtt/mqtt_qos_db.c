#include "mqtt_qos_db.h"
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/sqlite/sqlite3.h"
#include "supplemental/mqtt/mqtt_msg.h"

#define table_main "t_main"
#define table_msg "t_msg"
#define table_pipe_client "t_pipe_client"
#define table_client_msg "t_client_msg"
#define table_client_offline_msg "t_client_offline_msg"
#define table_client_info "t_client_info"

static uint8_t *nni_msg_serialize(nni_msg *msg, size_t *out_len);
static nni_msg *nni_msg_deserialize(uint8_t *bytes, size_t len);
static uint8_t *nni_mqtt_msg_serialize(nni_msg *msg, size_t *out_len);
static nni_msg *nni_mqtt_msg_deserialize(
    uint8_t *bytes, size_t len, bool aio_available);
static int      create_msg_table(sqlite3 *db);
static int      create_pipe_client_table(sqlite3 *db);
static int      create_main_table(sqlite3 *db);
static int      create_client_msg_table(sqlite3 *db);
static int      create_client_offline_msg_table(sqlite3 *db);
static int      create_client_info_table(sqlite3 *db);
static char *   get_db_path(
       char *dest_path, const char *user_path, const char *db_name);
static void    set_db_pragma(sqlite3 *db);
static void    remove_oldest_msg(
       sqlite3 *db, const char *table_name, const char *col_name, uint64_t limit);
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

static int
create_client_msg_table(sqlite3 *db)
{
	char sql[] = "CREATE TABLE IF NOT EXISTS " table_client_msg ""
	             " (id INTEGER PRIMARY KEY AUTOINCREMENT, "
	             "  packet_id INTEGER NOT NULL, "
	             "  pipe_id INTEGER NOT NULL, "
	             "  data BLOB, "
	             "  info_id INTEGER NOT NULL,"
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

static void
set_db_pragma(sqlite3 *db)
{
	sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, 0, 0);
	sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, 0, 0);
	sqlite3_exec(db, "PRAGMA wal_autocheckpoint", NULL, 0, 0);
}

static char *
get_db_path(char *dest_path, const char *user_path, const char *db_name)
{
	if (user_path == NULL) {
		char pwd[512] = { 0 };
		if (getcwd(pwd, sizeof(pwd)) != NULL) {
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
	sqlite3_bind_int(stmt, 2, packet_id);
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
	sqlite3_bind_int(stmt, 2, packet_id);
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
	sqlite3_bind_int(stmt, 4, packet_id);
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
	char sql[] = "DELETE FROM " table_msg
	             " WHERE id NOT IN (SELECT m_id FROM t_main)";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
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
	    "LIMIT 1";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int(stmt, 1, pipe_id);

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

	sqlite3_bind_int(stmt, 1, pipe_id);
	sqlite3_bind_int(stmt, 2, packet_id);
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

	sqlite3_bind_int(stmt, 1, pipe_id);
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
nni_mqtt_qos_db_set_client_msg(sqlite3 *db, uint32_t pipe_id,
    uint16_t packet_id, nni_msg *msg, const char *config_name)
{
	char sql[] =
	    "INSERT INTO " table_client_msg
	    " ( pipe_id, packet_id, data, info_id ) "
	    " VALUES (?, ?, ?, (SELECT id FROM " table_client_info
	    " WHERE config_name = ? LIMIT 1 ))";
	size_t   len  = 0;
	uint8_t *blob = nni_mqtt_msg_serialize(msg, &len);
	if (!blob) {
		printf("nni_mqtt_msg_serialize failed\n");
		return -1;
	}
	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int(stmt, 1, pipe_id);
	sqlite3_bind_int64(stmt, 2, packet_id);
	sqlite3_bind_blob64(stmt, 3, blob, len, SQLITE_TRANSIENT);
	sqlite3_bind_text(
	    stmt, 4, config_name, strlen(config_name), SQLITE_TRANSIENT);
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
	    "SELECT data FROM " table_client_msg ""
	    " WHERE pipe_id = ? AND packet_id = ? AND info_id = (SELECT id "
	    "FROM " table_client_info " WHERE config_name = ? LIMIT 1) ";
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, pipe_id);
	sqlite3_bind_int(stmt, 2, packet_id);
	sqlite3_bind_text(
	    stmt, 3, config_name, strlen(config_name), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		size_t   nbyte = (size_t) sqlite3_column_bytes16(stmt, 0);
		uint8_t *bytes = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 0), nbyte);
		// deserialize blob data to nni_msg
		msg = nni_mqtt_msg_deserialize(
		    bytes, nbyte, pipe_id > 0 ? true : false);
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
	sqlite3_bind_int(stmt, 2, packet_id);
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
	    "SELECT id, pipe_id, packet_id, data FROM " table_client_msg
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
		// deserialize blob data to nni_msg
		msg = nni_mqtt_msg_deserialize(
		    bytes, nbyte, pipe_id > 0 ? true : false);
		sqlite3_free(bytes);
	}
	sqlite3_finalize(stmt);

	sqlite3_exec(db, "COMMIT;", 0, 0, 0);

	return msg;
}

int
nni_mqtt_qos_db_set_client_offline_msg(
    sqlite3 *db, nni_msg *msg, const char *config_name)
{
	char sql[] =
	    "INSERT INTO " table_client_offline_msg " ( data , info_id ) "
	    "VALUES ( ?, (SELECT id FROM " table_client_info
	    " WHERE config_name = ? LIMIT 1 ))";
	size_t   len  = 0;
	uint8_t *blob = nni_mqtt_msg_serialize(msg, &len);

	if (!blob) {
		printf("nni_mqtt_msg_serialize failed\n");
		nni_msg_free(msg);
		return -1;
	}

	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_blob64(stmt, 1, blob, len, SQLITE_TRANSIENT);
	sqlite3_bind_text(
	    stmt, 2, config_name, strlen(config_name), SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	nng_free(blob, len);
	int rv = sqlite3_exec(db, "COMMIT;", 0, 0, 0);
	nni_msg_free(msg);
	return rv;
}

int
nni_mqtt_qos_db_set_client_offline_msg_batch(
    sqlite3 *db, nni_lmq *lmq, const char *config_name)
{
	int info_id = get_client_info_id(db, config_name);
	if(info_id < 0) {
		return -1;
	}

	char sql[] =
	    "INSERT INTO " table_client_offline_msg " ( data , info_id ) "
	    "VALUES ( ? , ? )";

	sqlite3_stmt *stmt;
	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	size_t lmq_len = nni_lmq_len(lmq);
	for (size_t i = 0; i < lmq_len; i++) {
		nni_msg *msg;
		if (nni_lmq_get(lmq, &msg) == 0) {
			size_t   len  = 0;
			uint8_t *blob = nni_mqtt_msg_serialize(msg, &len);
			if (!blob) {
				printf("nni_mqtt_msg_serialize failed\n");
				nni_msg_free(msg);
				continue;
			}
			sqlite3_reset(stmt);
			sqlite3_bind_blob64(
			    stmt, 1, blob, len, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, info_id);
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

	char sql[] = "SELECT id, data FROM " table_client_offline_msg
	             " WHERE info_id = (SELECT id FROM " table_client_info
	             " WHERE config_name = ? LIMIT 1) "
	             " ORDER BY id ASC LIMIT 1 ";

	sqlite3_exec(db, "BEGIN;", 0, 0, 0);
	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt)) {
		*row_id        = sqlite3_column_int64(stmt, 0);
		size_t   nbyte = (size_t) sqlite3_column_bytes16(stmt, 1);
		uint8_t *bytes = sqlite3_malloc(nbyte);
		memcpy(bytes, sqlite3_column_blob(stmt, 1), nbyte);
		// deserialize blob data to nni_msg
		msg = nni_mqtt_msg_deserialize(bytes, nbyte, false);
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

	sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, 0);
	sqlite3_reset(stmt);
	sqlite3_bind_text(
	    stmt, 1, config_name, strlen(config_name), SQLITE_TRANSIENT);
	if (SQLITE_ROW == sqlite3_step(stmt)) {
		id = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
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
nni_mqtt_msg_serialize(nni_msg *msg, size_t *out_len)
{
	// int rv;
	// if ((rv = nni_mqtt_msg_encode(msg)) != 0) {
	// 	printf("nni_mqtt_msg_encode failed: %d\n", rv);
	// 	return NULL;
	// }
	nni_mqtt_msg_encode(msg);

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
nni_mqtt_msg_deserialize(uint8_t *bytes, size_t len, bool aio_available)
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

	nni_mqtt_msg_decode(msg);

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

	return msg;

out:
	nni_msg_free(msg);
	return NULL;
}
