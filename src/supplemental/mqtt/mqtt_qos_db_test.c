#include <string.h>

#include "mqtt_msg.h"
#include "mqtt_qos_db.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
#include "nuts.h"
#include "nng/supplemental/nanolib/cvector.h"

#define test_db "test.db"
#define test_db_cache "test_bridge_cache.db"
#define test_db_legacy "test_bridge_legacy.db"

void
test_db_init(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_set(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	char *   header = "uvwxyz";
	char *   body   = "abcdefg";
	nni_time ts     = 1648004331;

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));
	nni_msg_set_timestamp(msg, ts);

	uint32_t pipe_id   = 1001;
	uint16_t packet_id = 999;
	uint8_t  qos       = 1;
	msg                = MQTT_DB_PACKED_MSG_QOS(msg, qos);
	nni_mqtt_qos_db_set(db, pipe_id, packet_id, msg);
	msg = MQTT_DB_GET_MSG_POINTER(msg);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_get(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	char *   header = "uvwxyz";
	char *   body   = "abcdefg";
	nni_time ts     = 1648004331;

	uint32_t pipe_id   = 1001;
	uint16_t packet_id = 999;

	nni_msg *msg = nni_mqtt_qos_db_get(db, pipe_id, packet_id);
	TEST_CHECK(MQTT_DB_GET_QOS_BITS(msg) == 1);
	// be careful nni_msg had been changed in nni_mqtt_qos_db_get();
	msg = MQTT_DB_GET_MSG_POINTER(msg);
	TEST_CHECK(strncmp(header, nni_msg_header(msg),
	               nni_msg_header_len(msg)) == 0);
	TEST_CHECK(strncmp(body, nni_msg_body(msg), nni_msg_len(msg)) == 0);
	TEST_CHECK(nni_msg_get_timestamp(msg) == ts);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_get_one(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);
	uint32_t pipe_id   = 1001;
	uint16_t packet_id = 999;
	nni_msg *msg       = nni_mqtt_qos_db_get_one(db, pipe_id, &packet_id);
	// be careful nni_msg had been changed in nni_mqtt_qos_db_get();
	msg = MQTT_DB_GET_MSG_POINTER(msg);
	TEST_CHECK(msg != NULL);
	TEST_CHECK(packet_id == 999);
	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_remove(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	nni_mqtt_qos_db_remove(db, 1001, 999);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_check_remove_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	char *header = "uvwxyz";
	char *body   = "abcdefg";

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));

	nni_mqtt_qos_db_check_remove_msg(db, msg);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
handle_cb(void *pipe_id, void *msg)
{
	TEST_CHECK(pipe_id != NULL);
	TEST_CHECK(msg != NULL);
}

void
test_qos_db_foreach(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);
	nni_mqtt_qos_db_foreach(db, handle_cb);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_remove_all_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);
	nni_mqtt_qos_db_remove_all_msg(db);
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_set(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);
	nni_mqtt_qos_db_set_pipe(db, 1001, "nanomq-client-1001");
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_remove(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);
	nni_mqtt_qos_db_remove_pipe(db, 1001);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_get_one_fifo(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	uint32_t pipe_id = 3221225473U; // > INT32_MAX: pins 64-bit binds
	nni_mqtt_qos_db_set_pipe(db, pipe_id, "nanomq-client-fifo");

	char *   header = "uvwxyz";
	nni_time ts     = 1648004331;
	// insertion order differs from packet id order on purpose,
	// fetch order must follow insertion order, not packet id order
	uint16_t packet_ids[] = { 3, 1, 2 };
	char *   bodies[]     = { "fifo-msg-a", "fifo-msg-b", "fifo-msg-c" };

	for (int i = 0; i < 3; i++) {
		nni_msg *msg;
		nni_msg_alloc(&msg, 0);
		nni_msg_header_append(msg, header, strlen(header));
		nni_msg_append(msg, bodies[i], strlen(bodies[i]));
		nni_msg_set_timestamp(msg, ts);
		msg = MQTT_DB_PACKED_MSG_QOS(msg, 1);
		nni_mqtt_qos_db_set(db, pipe_id, packet_ids[i], msg);
		msg = MQTT_DB_GET_MSG_POINTER(msg);
		nni_msg_free(msg);
	}

	for (int i = 0; i < 3; i++) {
		uint16_t packet_id = 0;
		nni_msg *msg =
		    nni_mqtt_qos_db_get_one(db, pipe_id, &packet_id);
		// be careful nni_msg had been changed in
		// nni_mqtt_qos_db_get_one();
		msg = MQTT_DB_GET_MSG_POINTER(msg);
		NUTS_ASSERT(msg != NULL);
		TEST_CHECK(packet_id == packet_ids[i]);
		TEST_CHECK(strncmp(bodies[i], nni_msg_body(msg),
		               nni_msg_len(msg)) == 0);
		// remove the returned row the same way the transport does
		nni_mqtt_qos_db_remove_msg(db, msg);
		nni_mqtt_qos_db_remove(db, pipe_id, packet_id);
		nni_msg_free(msg);
	}

	uint16_t packet_id = 0;
	TEST_CHECK(nni_mqtt_qos_db_get_one(db, pipe_id, &packet_id) == NULL);

	nni_mqtt_qos_db_remove_pipe(db, pipe_id);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_get_one_pipe_rebind(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	uint32_t old_pipe_id = 3221225474U; // > INT32_MAX
	uint32_t new_pipe_id = 3221225475U;
	uint16_t packet_id   = 77;
	char *   client_id   = "nanomq-client-rebind";
	char *   header      = "uvwxyz";
	char *   body        = "rebind-msg";

	nni_mqtt_qos_db_set_pipe(db, old_pipe_id, client_id);

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));
	nni_msg_set_timestamp(msg, 1648004331);
	msg = MQTT_DB_PACKED_MSG_QOS(msg, 1);
	nni_mqtt_qos_db_set(db, old_pipe_id, packet_id, msg);
	msg = MQTT_DB_GET_MSG_POINTER(msg);
	nni_msg_free(msg);

	// simulate a broker restart: all pipes are reset to 0, then the
	// client reconnects and its client id is bound to a new pipe id
	nni_mqtt_qos_db_update_all_pipe(db, 0);
	nni_mqtt_qos_db_set_pipe(db, new_pipe_id, client_id);

	uint16_t got_packet_id = 0;
	msg = nni_mqtt_qos_db_get_one(db, new_pipe_id, &got_packet_id);
	// be careful nni_msg had been changed in nni_mqtt_qos_db_get_one();
	msg = MQTT_DB_GET_MSG_POINTER(msg);
	NUTS_ASSERT(msg != NULL);
	TEST_CHECK(got_packet_id == packet_id);
	TEST_CHECK(strncmp(body, nni_msg_body(msg), nni_msg_len(msg)) == 0);

	nni_mqtt_qos_db_remove_msg(db, msg);
	nni_mqtt_qos_db_remove(db, new_pipe_id, got_packet_id);
	nni_msg_free(msg);
	nni_mqtt_qos_db_remove_pipe(db, new_pipe_id);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_remove_by_pipe(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	uint32_t pipe_id   = 3221225476U; // > INT32_MAX: pins 64-bit bind
	uint16_t packet_id = 11;
	char *   client_id = "nanomq-client-rm-by-pipe";
	char *   header    = "uvwxyz";
	char *   body      = "remove-by-pipe-msg";

	nni_mqtt_qos_db_set_pipe(db, pipe_id, client_id);

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));
	nni_msg_set_timestamp(msg, 1648004331);
	msg = MQTT_DB_PACKED_MSG_QOS(msg, 1);
	nni_mqtt_qos_db_set(db, pipe_id, packet_id, msg);
	msg = MQTT_DB_GET_MSG_POINTER(msg);
	nni_msg_free(msg);

	// the session-expiry cleanup path removes all rows of the pipe
	nni_mqtt_qos_db_remove_by_pipe(db, pipe_id);
	nni_mqtt_qos_db_remove_unused_msg(db);

	uint16_t got_packet_id = 0;
	msg = nni_mqtt_qos_db_get_one(db, pipe_id, &got_packet_id);
	NUTS_ASSERT(msg == NULL);

	nni_mqtt_qos_db_remove_pipe(db, pipe_id);
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_update_all(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);
	nni_mqtt_qos_db_update_all_pipe(db, 0);
	nni_mqtt_qos_db_close(db);
}

void
test_set_client_info(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);

	nni_mqtt_qos_db_set_client_info(
	    db, "nanomq", "client-2984792", "MQTT", 4);
	nni_mqtt_qos_db_set_client_info(
	    db, "emqx", "client-2984792", "MQTT", 4);
	nni_mqtt_qos_db_set_client_info(
	    db, "aws", "client-2984791", "MQTT", 4);

	nni_mqtt_qos_db_close(db);
}

void
test_set_client_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);

	nni_time ts        = 1650944298;
	uint32_t pipe_id   = 12345;
	uint16_t packet_id = 54321;

	nni_msg *msg;
	nni_mqtt_msg_alloc(&msg, 0);

	nni_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
	NUTS_TRUE(nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_CONNECT);
	nni_mqtt_msg_set_connect_client_id(msg, "nanomq-client-0FADECF");
	nni_mqtt_msg_set_connect_proto_version(msg, 4);

	char user[]   = "nanomq";
	char passwd[] = "nanomq";

	nng_mqtt_msg_set_connect_user_name(msg, user);
	nng_mqtt_msg_set_connect_password(msg, passwd);
	nng_mqtt_msg_set_connect_clean_session(msg, true);
	nng_mqtt_msg_set_connect_keep_alive(msg, 60);
	nni_msg_set_timestamp(msg, ts);

	nni_mqtt_msg_encode(msg);

	TEST_CHECK(
	    nni_mqtt_qos_db_set_client_msg(db, pipe_id, packet_id, msg, "emqx", 4) == 0);
	nni_mqtt_qos_db_close(db);
}

void
test_get_client_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);

	nni_time ts = 1650944298;

	nni_msg *msg = nni_mqtt_qos_db_get_client_msg(db, 12345, 54321, "emqx");
	TEST_CHECK(msg != NULL);
	TEST_CHECK(nni_mqtt_msg_get_packet_type(msg) == NNG_MQTT_CONNECT);
	TEST_CHECK(nni_mqtt_msg_get_connect_proto_version(msg) == 0x04);
	TEST_CHECK(nni_mqtt_msg_get_connect_keep_alive(msg) == 60);
	TEST_CHECK(strcmp(nni_mqtt_msg_get_connect_client_id(msg),
	               "nanomq-client-0FADECF") == 0);
	TEST_CHECK(
	    strcmp(nni_mqtt_msg_get_connect_user_name(msg), "nanomq") == 0);

	TEST_CHECK(nni_msg_get_timestamp(msg) == ts);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_remove_client_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);
	nni_mqtt_qos_db_remove_client_msg(db, 12345, 54321, "emqx");
	nni_mqtt_qos_db_close(db);
}

void
test_set_client_offline_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);

	nni_time ts = 1650944298;

	nni_msg *msg;
	nni_mqtt_msg_alloc(&msg, 0);

	nni_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
	NUTS_TRUE(nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_CONNECT);
	nni_mqtt_msg_set_connect_client_id(msg, "nanomq-client-0FADECF");
	nni_mqtt_msg_set_connect_proto_version(msg, 4);

	char user[]   = "nanomq";
	char passwd[] = "nanomq";

	nng_mqtt_msg_set_connect_user_name(msg, user);
	nng_mqtt_msg_set_connect_password(msg, passwd);
	nng_mqtt_msg_set_connect_clean_session(msg, true);
	nng_mqtt_msg_set_connect_keep_alive(msg, 60);
	nni_msg_set_timestamp(msg, ts);

	nni_mqtt_msg_encode(msg);

	TEST_CHECK(
	    nni_mqtt_qos_db_set_client_offline_msg(db, msg, "emqx", 4) == 0);
	nni_mqtt_qos_db_close(db);
}

void
test_get_client_offline_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);

	nni_time ts     = 1650944298;
	int64_t  row_id = 0;

	nni_msg *msg = nni_mqtt_qos_db_get_client_offline_msg(db, &row_id, "emqx");
	TEST_CHECK(msg != NULL);
	TEST_CHECK(nni_mqtt_msg_get_packet_type(msg) == NNG_MQTT_CONNECT);
	TEST_CHECK(nni_mqtt_msg_get_connect_proto_version(msg) == 0x04);
	TEST_CHECK(nni_mqtt_msg_get_connect_keep_alive(msg) == 60);
	TEST_CHECK(strcmp(nni_mqtt_msg_get_connect_client_id(msg),
	               "nanomq-client-0FADECF") == 0);
	TEST_CHECK(
	    strcmp(nni_mqtt_msg_get_connect_user_name(msg), "nanomq") == 0);

	TEST_CHECK(nni_msg_get_timestamp(msg) == ts);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_remove_client_offline_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);
	nni_mqtt_qos_db_remove_client_offline_msg(db, 1);
	nni_mqtt_qos_db_close(db);
}

void 
test_batch_insert_client_offline_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);

	nni_lmq lmq;
	nni_lmq_init(&lmq, 10);

	for (int i = 0; i < 10; i++) {
		nni_msg *msg;
		nni_mqtt_msg_alloc(&msg, 0);
		nni_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
		NUTS_TRUE(
		    nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_CONNECT);
		nni_mqtt_msg_set_connect_proto_version(msg, 4);
		nng_mqtt_msg_set_connect_keep_alive(msg, 60 + i);
		nni_mqtt_msg_encode(msg);
		nni_lmq_put(&lmq, msg);
	}

	TEST_CHECK(
	    nni_mqtt_qos_db_set_client_offline_msg_batch(db, &lmq, "emqx", 4) == 0);
	nni_lmq_fini(&lmq);
	nni_mqtt_qos_db_close(db);
}

static int
count_client_offline_rows(sqlite3 *db, const char *config_name)
{
	sqlite3_stmt *stmt = NULL;
	int           n    = -1;
	char sql[] = "SELECT count(*) FROM t_client_offline_msg WHERE info_id = "
	             "(SELECT id FROM t_client_info WHERE config_name = ? LIMIT 1)";

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, config_name, -1, SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			n = sqlite3_column_int(stmt, 0);
		}
	}
	sqlite3_finalize(stmt);
	return n;
}

/* The format version the per-node Bridge Cache records in PRAGMA user_version. */
static int
read_user_version(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	int           v    = -1;

	if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL) ==
	    SQLITE_OK) {
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			v = sqlite3_column_int(stmt, 0);
		}
	}
	sqlite3_finalize(stmt);
	return v;
}

void
test_remove_oldest_client_offline_msg(void)
{
	sqlite3 *db = NULL;
	char     sql[256];

	remove(test_db_cache);
	nni_mqtt_qos_db_init(&db, NULL, test_db_cache, false);

	// Two bridges with three cached messages each and distinct timestamps, so
	// the old ts-based statement would have deleted the other bridge's rows.
	sqlite3_exec(db,
	    "INSERT INTO t_client_info"
	    " (config_name) VALUES ('hub'), ('cloud');",
	    NULL, NULL, NULL);
	for (int i = 1; i <= 3; i++) {
		snprintf(sql, sizeof(sql),
		    "INSERT INTO t_client_offline_msg"
		    " (data, info_id, ts) VALUES ('hub%d', (SELECT id FROM "
		    " t_client_info"
		    " WHERE config_name = 'hub'), '2026-01-01 10:00:0%d');",
		    i, i);
		sqlite3_exec(db, sql, NULL, NULL, NULL);
		snprintf(sql, sizeof(sql),
		    "INSERT INTO t_client_offline_msg"
		    " (data, info_id, ts) VALUES ('cloud%d', (SELECT id FROM "
		    " t_client_info"
		    " WHERE config_name = 'cloud'), '2026-01-01 09:00:0%d');",
		    i, i);
		sqlite3_exec(db, sql, NULL, NULL, NULL);
	}

	// limit == 0 means "no cap": nothing may be removed.
	nni_mqtt_qos_db_remove_oldest_client_offline_msg(db, 0, "hub");
	NUTS_TRUE(count_client_offline_rows(db, "hub") == 3);
	NUTS_TRUE(count_client_offline_rows(db, "cloud") == 3);

	// Keeping only the newest 2 of "hub" must not touch "cloud" (#2445).
	nni_mqtt_qos_db_remove_oldest_client_offline_msg(db, 2, "hub");
	NUTS_TRUE(count_client_offline_rows(db, "hub") == 2);
	NUTS_TRUE(count_client_offline_rows(db, "cloud") == 3);

	nni_mqtt_qos_db_close(db);
	remove(test_db_cache);
}

void
test_migrate_legacy_bridge_cache(void)
{
	const char *dir = ".";
	sqlite3    *db  = NULL;
	sqlite3    *legacy = NULL;
	char        sql[512];

	remove(test_db_cache);
	remove(test_db_legacy);

	// The pre-per-node shared database: two bridges in one file.
	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	sqlite3_exec(legacy,
	    "INSERT INTO t_client_info (config_name) VALUES ('hub'), ('cloud');",
	    NULL, NULL, NULL);
	for (int i = 1; i <= 2; i++) {
		snprintf(sql, sizeof(sql),
		    "INSERT INTO t_client_offline_msg (data, info_id, ts) VALUES "
		    "('hub%d', (SELECT id FROM t_client_info WHERE "
		    "config_name = 'hub'), '2026-01-01 10:00:0%d');",
		    i, i);
		sqlite3_exec(legacy, sql, NULL, NULL, NULL);
	}
	sqlite3_exec(legacy,
	    "INSERT INTO t_client_offline_msg (data, info_id, ts) VALUES "
	    "('cloud1', (SELECT id FROM t_client_info WHERE "
	    "config_name = 'cloud'), '2026-01-01 09:00:01');",
	    NULL, NULL, NULL);
	nni_mqtt_qos_db_close(legacy);

	// First start of "hub"'s own file: its two rows are carried over.
	nni_mqtt_qos_db_init(&db, dir, test_db_cache, false);
	nni_mqtt_qos_db_set_client_info(db, "hub", NULL, "MQTT", 4);
	nni_mqtt_qos_db_migrate_legacy(db, dir, test_db_legacy, "hub");
	NUTS_TRUE(count_client_offline_rows(db, "hub") == 2);

	// Running it again copies nothing: the legacy rows were deleted.
	nni_mqtt_qos_db_migrate_legacy(db, dir, test_db_legacy, "hub");
	NUTS_TRUE(count_client_offline_rows(db, "hub") == 2);
	nni_mqtt_qos_db_close(db);

	// The other bridge's rows are left for its own migration.
	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	NUTS_TRUE(count_client_offline_rows(legacy, "hub") == 0);
	NUTS_TRUE(count_client_offline_rows(legacy, "cloud") == 1);
	nni_mqtt_qos_db_close(legacy);

	remove(test_db_cache);
	remove(test_db_legacy);
}

/**
 * A deployment that never set mounted_file_path keeps its cache in the working
 * directory. The legacy lookup has to fall back there as well, otherwise the
 * backlog of such an installation is never moved.
 */
void
test_migrate_legacy_with_default_path(void)
{
	const char *name   = "hub";
	sqlite3    *db     = NULL;
	sqlite3    *legacy = NULL;
	char        sql[512];

	remove(test_db_cache);
	remove(test_db_legacy);

	nni_mqtt_qos_db_init(&legacy, NULL, test_db_legacy, false);
	sqlite3_exec(legacy,
	    "INSERT INTO t_client_info (config_name) VALUES ('hub');",
	    NULL, NULL, NULL);
	for (int i = 1; i <= 2; i++) {
		snprintf(sql, sizeof(sql),
		    "INSERT INTO t_client_offline_msg (data, info_id, ts) VALUES "
		    "('hub%d', (SELECT id FROM t_client_info WHERE "
		    "config_name = 'hub'), '2026-01-01 10:00:0%d');",
		    i, i);
		sqlite3_exec(legacy, sql, NULL, NULL, NULL);
	}
	nni_mqtt_qos_db_close(legacy);

	nni_mqtt_qos_db_init(&db, NULL, test_db_cache, false);
	nni_mqtt_qos_db_set_client_info(db, name, NULL, "MQTT", 4);
	nni_mqtt_qos_db_migrate_legacy(db, NULL, test_db_legacy, name);

	NUTS_TRUE(count_client_offline_rows(db, name) == 2);
	NUTS_TRUE(read_user_version(db) == 1);
	nni_mqtt_qos_db_close(db);

	nni_mqtt_qos_db_init(&legacy, NULL, test_db_legacy, false);
	NUTS_TRUE(count_client_offline_rows(legacy, name) == 0);
	nni_mqtt_qos_db_close(legacy);

	remove(test_db_cache);
	remove(test_db_legacy);
}

/**
 * A start that created the per-node file but never finished the migration must
 * not strand the legacy rows: the next start has to pick them up again.
 */
void
test_migrate_legacy_retry_after_failed_start(void)
{
	const char *dir  = ".";
	const char *name = "hub";
	sqlite3    *db    = NULL;
	sqlite3    *legacy = NULL;
	char        sql[512];

	conf_sqlite             sq    = { 0 };
	int                     dummy = 0;
	nng_mqtt_sqlite_option *opt   = NULL;

	remove(test_db_cache);
	remove(test_db_legacy);

	// The per-node file already exists, as it would after a start that died
	// between creating it and migrating.
	nni_mqtt_qos_db_init(&db, dir, test_db_cache, false);
	nni_mqtt_qos_db_set_client_info(db, name, NULL, "MQTT", 4);
	nni_mqtt_qos_db_close(db);

	// And the legacy rows are still sitting there.
	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	sqlite3_exec(legacy,
	    "INSERT INTO t_client_info (config_name) VALUES ('hub');",
	    NULL, NULL, NULL);
	for (int i = 1; i <= 2; i++) {
		snprintf(sql, sizeof(sql),
		    "INSERT INTO t_client_offline_msg (data, info_id, ts) VALUES "
		    "('hub%d', (SELECT id FROM t_client_info WHERE "
		    "config_name = 'hub'), '2026-01-01 10:00:0%d');",
		    i, i);
		sqlite3_exec(legacy, sql, NULL, NULL, NULL);
	}
	nni_mqtt_qos_db_close(legacy);

	// The next start runs the migration again and carries them over.
	sq.enable              = true;
	sq.mounted_file_path   = (char *) dir;
	sq.flush_mem_threshold = 1;

	NUTS_PASS(nng_mqtt_alloc_sqlite_opt(&opt));
	nng_mqtt_set_sqlite_conf(opt, &dummy, name, 4, &sq);
	nng_mqtt_sqlite_db_init_legacy(opt, test_db_cache, dir, test_db_legacy);
	nni_mqtt_sqlite_db_fini(opt);
	nng_mqtt_free_sqlite_opt(opt);

	nni_mqtt_qos_db_init(&db, dir, test_db_cache, false);
	NUTS_TRUE(count_client_offline_rows(db, name) == 2);
	nni_mqtt_qos_db_close(db);

	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	NUTS_TRUE(count_client_offline_rows(legacy, name) == 0);
	nni_mqtt_qos_db_close(legacy);

	remove(test_db_cache);
	remove(test_db_legacy);
}

/**
 * Once the move has finished the node database says so, and later starts skip
 * it instead of reading the legacy file again.
 */
void
test_migrate_legacy_skipped_once_marked(void)
{
	const char *dir  = ".";
	const char *name = "hub";
	sqlite3    *db    = NULL;
	sqlite3    *legacy = NULL;
	char        sql[512];

	conf_sqlite             sq    = { 0 };
	int                     dummy = 0;
	nng_mqtt_sqlite_option *opt   = NULL;

	remove(test_db_cache);
	remove(test_db_legacy);

	// A legacy file holding two rows for this bridge.
	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	sqlite3_exec(legacy,
	    "INSERT INTO t_client_info (config_name) VALUES ('hub');",
	    NULL, NULL, NULL);
	for (int i = 1; i <= 2; i++) {
		snprintf(sql, sizeof(sql),
		    "INSERT INTO t_client_offline_msg (data, info_id, ts) VALUES "
		    "('hub%d', (SELECT id FROM t_client_info WHERE "
		    "config_name = 'hub'), '2026-01-01 10:00:0%d');",
		    i, i);
		sqlite3_exec(legacy, sql, NULL, NULL, NULL);
	}
	nni_mqtt_qos_db_close(legacy);

	sq.enable              = true;
	sq.mounted_file_path   = (char *) dir;
	sq.flush_mem_threshold = 1;

	// First start: the rows move and the file records that it is done.
	NUTS_PASS(nng_mqtt_alloc_sqlite_opt(&opt));
	nng_mqtt_set_sqlite_conf(opt, &dummy, name, 4, &sq);
	nng_mqtt_sqlite_db_init_legacy(opt, test_db_cache, dir, test_db_legacy);
	nni_mqtt_sqlite_db_fini(opt);
	nng_mqtt_free_sqlite_opt(opt);

	nni_mqtt_qos_db_init(&db, dir, test_db_cache, false);
	NUTS_TRUE(count_client_offline_rows(db, name) == 2);
	NUTS_TRUE(read_user_version(db) == 1);
	nni_mqtt_qos_db_close(db);

	// A row appears in the legacy file afterwards, as a restored backup would.
	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	sqlite3_exec(legacy,
	    "INSERT INTO t_client_offline_msg (data, info_id, ts) VALUES "
	    "('late', (SELECT id FROM t_client_info WHERE config_name = 'hub'), "
	    "'2026-01-02 10:00:00');",
	    NULL, NULL, NULL);
	nni_mqtt_qos_db_close(legacy);

	// Second start: marked, so the migration is skipped and nothing moves.
	opt = NULL;
	NUTS_PASS(nng_mqtt_alloc_sqlite_opt(&opt));
	nng_mqtt_set_sqlite_conf(opt, &dummy, name, 4, &sq);
	nng_mqtt_sqlite_db_init_legacy(opt, test_db_cache, dir, test_db_legacy);
	nni_mqtt_sqlite_db_fini(opt);
	nng_mqtt_free_sqlite_opt(opt);

	nni_mqtt_qos_db_init(&db, dir, test_db_cache, false);
	NUTS_TRUE(count_client_offline_rows(db, name) == 2);
	nni_mqtt_qos_db_close(db);

	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	NUTS_TRUE(count_client_offline_rows(legacy, name) == 1);
	nni_mqtt_qos_db_close(legacy);

	remove(test_db_cache);
	remove(test_db_legacy);
}

/**
 * A migration that cannot open its transaction must leave both sides alone.
 * Copying without deleting would duplicate the messages on the next start.
 */
void
test_migrate_legacy_aborts_when_legacy_is_locked(void)
{
	const char *dir     = ".";
	const char *name    = "hub";
	sqlite3    *db      = NULL;
	sqlite3    *legacy  = NULL;
	sqlite3    *blocker = NULL;
	char        sql[512];

	remove(test_db_cache);
	remove(test_db_legacy);

	// Two rows waiting in the legacy file.
	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	sqlite3_exec(legacy,
	    "INSERT INTO t_client_info (config_name) VALUES ('hub');",
	    NULL, NULL, NULL);
	for (int i = 1; i <= 2; i++) {
		snprintf(sql, sizeof(sql),
		    "INSERT INTO t_client_offline_msg (data, info_id, ts) VALUES "
		    "('hub%d', (SELECT id FROM t_client_info WHERE "
		    "config_name = 'hub'), '2026-01-01 10:00:0%d');",
		    i, i);
		sqlite3_exec(legacy, sql, NULL, NULL, NULL);
	}
	nni_mqtt_qos_db_close(legacy);

	nni_mqtt_qos_db_init(&db, dir, test_db_cache, false);
	nni_mqtt_qos_db_set_client_info(db, name, NULL, "MQTT", 4);

	// A second connection holds the legacy file, the way a second broker on
	// the same spool directory would. This makes the migration wait out its
	// busy timeout before giving up.
	NUTS_TRUE(sqlite3_open(test_db_legacy, &blocker) == SQLITE_OK);
	NUTS_TRUE(sqlite3_exec(blocker, "BEGIN IMMEDIATE;", 0, 0, 0) ==
	    SQLITE_OK);

	nni_mqtt_qos_db_migrate_legacy(db, dir, test_db_legacy, name);

	// Nothing was copied, and the legacy rows are untouched.
	NUTS_TRUE(count_client_offline_rows(db, name) == 0);
	NUTS_TRUE(read_user_version(db) == 0);
	sqlite3_exec(blocker, "ROLLBACK;", 0, 0, 0);
	sqlite3_close(blocker);
	nni_mqtt_qos_db_close(db);

	nni_mqtt_qos_db_init(&legacy, dir, test_db_legacy, false);
	NUTS_TRUE(count_client_offline_rows(legacy, name) == 2);
	nni_mqtt_qos_db_close(legacy);

	remove(test_db_cache);
	remove(test_db_legacy);
}

void 
test_set_retain_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	nni_msg *msg = NULL;
	nni_msg_alloc(&msg, 0);
	nni_msg_append(msg, "hello", 5);

	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "topic1/2/3", msg, 4) == 0);

	nni_mqtt_qos_db_close(db);
	nni_msg_free(msg);
}

void 
test_get_retain_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	nni_msg *msg = nni_mqtt_qos_db_get_retain(db, "topic1/2/3");

	NUTS_ASSERT(msg != NULL);

	NUTS_TRUE(strcmp(nni_msg_body(msg), "hello") == 0);

	nni_msg_free(msg);

	nni_mqtt_qos_db_close(db);
}

void 
test_find_retain_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	nni_msg **msgs = nni_mqtt_qos_db_find_retain(db, "topic1/#");

	NUTS_ASSERT(msgs != NULL);

	for (size_t i = 0; i < cvector_size(msgs); i++) {
		nni_msg *msg = msgs[i];
		NUTS_TRUE(strcmp(nni_msg_body(msg), "hello") == 0);
		nni_msg_free(msg);
	}

	nni_mqtt_qos_db_close(db);
	cvector_free(msgs);
}


void
test_remove_retain_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, true);

	NUTS_TRUE(nni_mqtt_qos_db_remove_retain(db, "topic1/2/3") == 0);

	nni_mqtt_qos_db_close(db);
}

TEST_LIST = {
	{ "db_init", test_db_init },
	{ "db_pipe_set", test_pipe_set },
	{ "db_set", test_qos_db_set },
	{ "db_get", test_qos_db_get },
	{ "db_get_one", test_qos_db_get_one },
	{ "db_foreach", test_qos_db_foreach },
	{ "db_remove_all_msg", test_qos_db_remove_all_msg },
	{ "db_remove", test_qos_db_remove },
	{ "db_check_remove_msg", test_qos_db_check_remove_msg },
	{ "db_pipe_remove", test_pipe_remove },
	{ "db_get_one_fifo", test_qos_db_get_one_fifo },
	{ "db_get_one_pipe_rebind", test_qos_db_get_one_pipe_rebind },
	{ "db_remove_by_pipe", test_qos_db_remove_by_pipe },
	{ "db_set_retain", test_set_retain_msg },
	{ "db_get_retain", test_get_retain_msg },
	{ "db_find_retain", test_find_retain_msg },
	{ "db_remove_retain", test_remove_retain_msg },
	{ "db_set_client_info", test_set_client_info },
	{ "db_set_client_msg", test_set_client_msg },
	{ "db_get_client_msg", test_get_client_msg },
	{ "db_remove_client_msg", test_remove_client_msg },
	{ "db_set_client_offline_msg", test_set_client_offline_msg },
	{ "db_get_client_offline_msg", test_get_client_offline_msg },
	{ "db_remove_client_offline_msg", test_remove_client_offline_msg },
	{ "db_batch_insert_client_offline_msg",
	    test_batch_insert_client_offline_msg },
	{ "db_remove_oldest_client_offline_msg",
	    test_remove_oldest_client_offline_msg },
	{ "db_migrate_legacy_bridge_cache", test_migrate_legacy_bridge_cache },
	{ "db_migrate_legacy_retry_after_failed_start",
	    test_migrate_legacy_retry_after_failed_start },
	{ "db_migrate_legacy_skipped_once_marked",
	    test_migrate_legacy_skipped_once_marked },
	{ "db_migrate_legacy_aborts_when_legacy_is_locked",
	    test_migrate_legacy_aborts_when_legacy_is_locked },
	{ "db_migrate_legacy_with_default_path",
	    test_migrate_legacy_with_default_path },
	{ NULL, NULL },
};
