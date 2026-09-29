#include <stdio.h>
#include <string.h>

#include "mqtt_msg.h"
#include "mqtt_qos_db.h"
#include "nng/nng.h"
#include "nuts.h"
#include "nng/supplemental/nanolib/cvector.h"

#define test_db "test.db"

// Timed waits compare against nni_clock(), but the condition variables only
// share that clock's base once the platform has been initialised:
// nni_plat_init() is what sets the global condvar attribute to
// NNG_USE_CLOCKID.  Without this call every nni_cv_until() returns
// immediately, so the batcher flushes on each pass instead of waiting out its
// interval.  The broker always runs initialised; tests that assert on
// deferral have to as well.
static void
test_platform_init(void)
{
	(void) nni_init();
}

// The buffered retain path gets its own database so the ordering-sensitive
// synchronous tests above are not disturbed by leftovers.
#define test_retain_db "test_retain.db"
#define test_retain_db2 "test_retain_second.db"
#define CONC_THREADS 4
#define CONC_TOPICS 64

void
test_db_init(void)
{
	sqlite3 *db = NULL;
	test_platform_init();
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

void
test_remove_oldest_client_offline_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, NULL, test_db, false);
	nni_mqtt_qos_db_remove_oldest_client_offline_msg(db, 0, "emqx");
	nni_mqtt_qos_db_close(db);
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

// ---- buffered retain store ----------------------------------------------

static nni_msg *
retain_test_msg(const char *body)
{
	nni_msg *msg = NULL;

	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, "uvwxyz", 6);
	nni_msg_append(msg, body, strlen(body));
	nni_msg_set_timestamp(msg, 1648004331);
	return (msg);
}

static void
retain_db_reset(void)
{
	// Each batch test starts from a clean process-global batcher.  A real
	// broker never does this: it leaves the lock and buffer alive after a
	// shutdown, so production's singleton is one-shot (mqtt_qos_db.c).
	nni_mqtt_qos_db_retain_batch_reset_for_test();
	remove(test_retain_db);
	remove(test_retain_db "-wal");
	remove(test_retain_db "-shm");
}

// The batcher reads its threshold and interval from this struct on every
// tick, so a test can rewrite the fields to stand in for a config reload.
static conf_sqlite test_sqlite_conf;

static void
retain_batch_setup(sqlite3 *db, size_t threshold, uint64_t interval_ms)
{
	test_sqlite_conf.retain_flush_threshold = threshold;
	test_sqlite_conf.flush_interval        = interval_ms;
	nni_mqtt_qos_db_retain_batch_setup(db, &test_sqlite_conf);
}

// Read a table through a second connection, which is the only way to see what
// has actually reached the file rather than what is still buffered.
static int64_t
retain_count_of(const char *path)
{
	sqlite3      *db   = NULL;
	sqlite3_stmt *stmt = NULL;
	int64_t       n    = -1;

	if (sqlite3_open(path, &db) != SQLITE_OK) {
		sqlite3_close(db);
		return (-1);
	}
	if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM t_retain", -1,
	        &stmt, 0) == SQLITE_OK &&
	    sqlite3_step(stmt) == SQLITE_ROW) {
		n = sqlite3_column_int64(stmt, 0);
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return (n);
}

static int64_t
retain_count(void)
{
	return (retain_count_of(test_retain_db));
}

typedef struct conc_arg {
	sqlite3 *db;
	int      idx;
} conc_arg;

static void
conc_topic_worker(void *arg)
{
	conc_arg *a = arg;

	for (int i = 0; i < CONC_TOPICS; i++) {
		char     topic[64];
		char     body[32];
		nni_msg *m;

		snprintf(topic, sizeof(topic), "conc/%d/%d", a->idx, i);
		snprintf(body, sizeof(body), "t%d-%d", a->idx, i);
		m = retain_test_msg(body);
		nni_mqtt_qos_db_set_retain(a->db, topic, m, 4);
		nni_msg_free(m);
	}
}

static void
conc_same_topic_worker(void *arg)
{
	conc_arg *a = arg;

	for (int i = 0; i < 200; i++) {
		char     body[32];
		nni_msg *m;

		snprintf(body, sizeof(body), "t%d-%d", a->idx, i);
		m = retain_test_msg(body);
		nni_mqtt_qos_db_set_retain(a->db, "conc/shared", m, 4);
		nni_msg_free(m);
	}
}

static void
run_workers(sqlite3 *db, void (*fn)(void *))
{
	nni_thr  thr[CONC_THREADS];
	conc_arg args[CONC_THREADS];

	for (int i = 0; i < CONC_THREADS; i++) {
		args[i].db  = db;
		args[i].idx = i;
		nni_thr_init(&thr[i], fn, &args[i]);
	}
	for (int i = 0; i < CONC_THREADS; i++) {
		nni_thr_run(&thr[i]);
	}
	for (int i = 0; i < CONC_THREADS; i++) {
		nni_thr_fini(&thr[i]);
	}
}

// A buffered store must answer reads before it reaches the file.
void
test_retain_batch_visible(void)
{
	sqlite3 *db  = NULL;
	nni_msg *msg = retain_test_msg("hello");
	nni_msg *got;
	nni_msg **vec;

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "a/b/c", msg, 4) == 0);
	nni_msg_free(msg);

	// not flushed yet, but a pattern query must still find it
	vec = nni_mqtt_qos_db_find_retain(db, "a/#");
	NUTS_ASSERT(vec != NULL);
	NUTS_TRUE(cvector_size(vec) == 1);
	NUTS_TRUE(strcmp((const char *) nni_msg_body(vec[0]), "hello") == 0);
	nni_msg_free(vec[0]);
	cvector_free(vec);

	got = nni_mqtt_qos_db_get_retain(db, "a/b/c");
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(strcmp((const char *) nni_msg_body(got), "hello") == 0);
	nni_msg_free(got);

	nni_mqtt_qos_db_close(db);
}

// A store and its removal inside one window end with the topic gone.  The
// row may be written and deleted within the single flush transaction; what
// matters is the end state.
void
test_retain_batch_set_then_clear(void)
{
	sqlite3 *db  = NULL;
	nni_msg *msg = retain_test_msg("vanish");

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "x/1", msg, 4) == 0);
	nni_msg_free(msg);
	NUTS_TRUE(nni_mqtt_qos_db_remove_retain(db, "x/1") == 0);

	nni_mqtt_qos_db_close(db);

	NUTS_TRUE(retain_count() == 0);

	// and a later lookup must answer NULL rather than a stale row
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	NUTS_TRUE(nni_mqtt_qos_db_get_retain(db, "x/1") == NULL);
	nni_mqtt_qos_db_close(db);
}

// A value that already reached the file must still be removed by a later
// clear, even when that clear shares a flush window with a newer store.
// Regression test: the buffer used to cancel the pair outright whenever a
// payload was pending, which left the flushed row serving stale state.
void
test_retain_batch_clear_after_flush(void)
{
	sqlite3 *db  = NULL;
	nni_msg *msg;
	nni_msg **vec;

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	// v1 reaches the file (find_retain flushes ahead of its query)
	msg = retain_test_msg("v1");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "r/1", msg, 4) == 0);
	nni_msg_free(msg);
	vec = nni_mqtt_qos_db_find_retain(db, "r/#");
	NUTS_ASSERT(vec != NULL);
	NUTS_TRUE(cvector_size(vec) == 1);
	nni_msg_free(vec[0]);
	cvector_free(vec);
	NUTS_TRUE(retain_count() == 1);

	// v2 is buffered, then cleared before the next flush
	msg = retain_test_msg("v2");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "r/1", msg, 4) == 0);
	nni_msg_free(msg);
	NUTS_TRUE(nni_mqtt_qos_db_remove_retain(db, "r/1") == 0);

	nni_mqtt_qos_db_close(db);

	NUTS_TRUE(retain_count() == 0);
}

// Repeated stores for one topic collapse to the last one.
void
test_retain_batch_last_write_wins(void)
{
	sqlite3 *db = NULL;
	nni_msg *got;

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	for (int i = 0; i < 5; i++) {
		char     body[16];
		nni_msg *m;

		snprintf(body, sizeof(body), "v%d", i);
		m = retain_test_msg(body);
		nni_mqtt_qos_db_set_retain(db, "same/topic", m, 4);
		nni_msg_free(m);
	}
	nni_mqtt_qos_db_close(db);

	NUTS_TRUE(retain_count() == 1);

	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	got = nni_mqtt_qos_db_get_retain(db, "same/topic");
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(strcmp((const char *) nni_msg_body(got), "v4") == 0);
	nni_msg_free(got);
	nni_mqtt_qos_db_close(db);
}

// Reaching the count threshold flushes without waiting for the interval.
void
test_retain_batch_count_trigger(void)
{
	sqlite3 *db = NULL;
	int64_t  n  = 0;

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	// a tiny threshold with a long interval, so only the count can trigger
	retain_batch_setup(db, 2, 60000);

	for (int i = 0; i < 3; i++) {
		char     topic[24];
		nni_msg *m;

		snprintf(topic, sizeof(topic), "count/%d", i);
		m = retain_test_msg("x");
		nni_mqtt_qos_db_set_retain(db, topic, m, 4);
		nni_msg_free(m);
	}
	for (int i = 0; i < 200; i++) {
		n = retain_count();
		if (n >= 2) {
			break;
		}
		nni_msleep(10);
	}
	nni_mqtt_qos_db_close(db);

	NUTS_TRUE(n >= 2);
}

// threshold == 0 leaves the store synchronous.
void
test_retain_batch_disabled_is_synchronous(void)
{
	sqlite3 *db  = NULL;
	nni_msg *msg = retain_test_msg("now");

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 0, 100);

	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "sync/1", msg, 4) == 0);
	nni_msg_free(msg);

	// on disk immediately, before any close
	NUTS_TRUE(retain_count() == 1);
	nni_mqtt_qos_db_close(db);
}

// Several threads storing disjoint topic sets: every topic must survive.
void
test_retain_batch_concurrent_topics(void)
{
	sqlite3 *db = NULL;

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	run_workers(db, conc_topic_worker);
	nni_mqtt_qos_db_close(db);

	NUTS_TRUE(retain_count() == (int64_t) CONC_THREADS * CONC_TOPICS);
}

// Several threads hammering one topic: exactly one row survives, and it is a
// complete write rather than a torn or duplicated one.
void
test_retain_batch_concurrent_same_topic(void)
{
	sqlite3 *db = NULL;
	nni_msg *got;

	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	run_workers(db, conc_same_topic_worker);
	nni_mqtt_qos_db_close(db);

	NUTS_TRUE(retain_count() == 1);

	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	got = nni_mqtt_qos_db_get_retain(db, "conc/shared");
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(((const char *) nni_msg_body(got))[0] == 't');
	nni_msg_free(got);
	nni_mqtt_qos_db_close(db);
}

// Batching belongs to one database per process.  A second database asking for
// it must be left on the synchronous path, and closing that database must not
// disturb the buffer the first one owns.
void
test_retain_batch_second_db_ignored(void)
{
	sqlite3 *db1 = NULL, *db2 = NULL;
	nni_msg *msg;

	test_platform_init();
	retain_db_reset();
	remove(test_retain_db2);
	remove(test_retain_db2 "-wal");
	remove(test_retain_db2 "-shm");

	nni_mqtt_qos_db_init(&db1, NULL, test_retain_db, true);
	retain_batch_setup(db1, 1000, 60000);

	// second database asks for batching while the first one holds it
	nni_mqtt_qos_db_init(&db2, NULL, test_retain_db2, true);
	retain_batch_setup(db2, 5, 1000);

	msg = retain_test_msg("first");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db1, "d1/t", msg, 4) == 0);
	nni_msg_free(msg);

	msg = retain_test_msg("second");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db2, "d2/t", msg, 4) == 0);
	nni_msg_free(msg);

	// db1 still batches, so nothing has reached its file yet; db2 fell back
	// to the synchronous path and is already durable
	NUTS_TRUE(retain_count_of(test_retain_db) == 0);
	NUTS_TRUE(retain_count_of(test_retain_db2) == 1);

	// closing the other database must not strand db1's buffer
	nni_mqtt_qos_db_close(db2);
	NUTS_TRUE(retain_count_of(test_retain_db) == 0);

	// closing the batched handle drains it
	nni_mqtt_qos_db_close(db1);
	NUTS_TRUE(retain_count_of(test_retain_db) == 1);
}

// A config reload rewrites retain_flush_threshold / flush_interval in place;
// the batcher re-reads them, so a change applies without a restart.
void
test_retain_batch_reconfigured(void)
{
	sqlite3 *db  = NULL;
	nni_msg *msg = retain_test_msg("live");
	int64_t  n   = 0;

	test_platform_init();
	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	// wide open: nothing flushes on its own
	retain_batch_setup(db, 1000, 60000);

	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "re/1", msg, 4) == 0);
	nni_msg_free(msg);
	NUTS_TRUE(retain_count() == 0);

	// stand in for a reload that drops the threshold to one pending entry
	test_sqlite_conf.retain_flush_threshold = 1;
	msg = retain_test_msg("after-reload");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "re/2", msg, 4) == 0);
	nni_msg_free(msg);

	for (int i = 0; i < 200; i++) {
		n = retain_count();
		if (n >= 2) {
			break;
		}
		nni_msleep(10);
	}
	NUTS_TRUE(n >= 2);

	nni_mqtt_qos_db_close(db);
}

// A reload that sets retain_flush_threshold = 0 cannot disable batching at
// runtime: the batcher keeps its last value rather than degenerating.
void
test_retain_batch_runtime_disable_refused(void)
{
	sqlite3 *db  = NULL;
	nni_msg *msg;

	test_platform_init();
	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	// stand in for the reload
	test_sqlite_conf.retain_flush_threshold = 0;

	msg = retain_test_msg("buffered");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "off/1", msg, 4) == 0);
	nni_msg_free(msg);

	// still buffered, not spilled to the synchronous path
	NUTS_TRUE(retain_count() == 0);
	nni_mqtt_qos_db_close(db);
	NUTS_TRUE(retain_count() == 1);
}

// After the batcher is shut down the database may still be open -- that is
// exactly what the broker's exit path leaves behind.  Every entry point must
// fall back to the synchronous path instead of touching the retired buffer.
void
test_retain_batch_after_shutdown(void)
{
	sqlite3 *db  = NULL;
	nni_msg *msg;
	nni_msg *got;
	nni_msg **vec;

	test_platform_init();
	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	msg = retain_test_msg("before");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "sh/1", msg, 4) == 0);
	nni_msg_free(msg);

	// Shut the batcher down while the database stays open.
	nni_mqtt_qos_db_retain_batch_shutdown_for_test();
	NUTS_TRUE(retain_count() == 1); // drained on shutdown

	// the same handle is now on the synchronous path
	msg = retain_test_msg("after");
	NUTS_TRUE(nni_mqtt_qos_db_set_retain(db, "sh/2", msg, 4) == 0);
	nni_msg_free(msg);
	NUTS_TRUE(retain_count() == 2);

	NUTS_TRUE(nni_mqtt_qos_db_remove_retain(db, "sh/1") == 0);
	NUTS_TRUE(retain_count() == 1);

	got = nni_mqtt_qos_db_get_retain(db, "sh/2");
	NUTS_ASSERT(got != NULL);
	NUTS_TRUE(strcmp((const char *) nni_msg_body(got), "after") == 0);
	nni_msg_free(got);

	vec = nni_mqtt_qos_db_find_retain(db, "sh/#");
	NUTS_ASSERT(vec != NULL);
	NUTS_TRUE(cvector_size(vec) == 1);
	nni_msg_free(vec[0]);
	cvector_free(vec);

	nni_mqtt_qos_db_close(db);
}

typedef struct race_arg {
	sqlite3   *db;
	volatile int stop;
} race_arg;

static void
race_pub_worker(void *arg)
{
	race_arg *a = arg;

	while (!a->stop) {
		nni_msg *m = retain_test_msg("race");
		(void) nni_mqtt_qos_db_set_retain(a->db, "race/t", m, 4);
		nni_msg_free(m);
	}
}

// Publishers race the batcher being switched off.  A caller that already
// passed the unlocked enabled check must not touch a torn-down buffer, and a
// later synchronous write must not be clobbered by a stale buffered value.
void
test_retain_batch_shutdown_race(void)
{
	sqlite3 *db = NULL;
	nni_thr  thr[4];
	race_arg args[4];

	test_platform_init();
	retain_db_reset();
	nni_mqtt_qos_db_init(&db, NULL, test_retain_db, true);
	retain_batch_setup(db, 1000, 60000);

	for (int i = 0; i < 4; i++) {
		args[i].db   = db;
		args[i].stop = 0;
		nni_thr_init(&thr[i], race_pub_worker, &args[i]);
	}
	for (int i = 0; i < 4; i++) {
		nni_thr_run(&thr[i]);
	}

	nni_msleep(20);
	// switch the batcher off underneath the publishers; the database stays
	// open, so the fallback is a valid synchronous write
	nni_mqtt_qos_db_retain_batch_shutdown_for_test();

	for (int i = 0; i < 4; i++) {
		args[i].stop = 1;
	}
	for (int i = 0; i < 4; i++) {
		nni_thr_fini(&thr[i]);
	}

	// survived the race, and the single topic holds exactly one row
	NUTS_TRUE(retain_count() == 1);
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
	{ "db_retain_batch_visible", test_retain_batch_visible },
	{ "db_retain_batch_set_then_clear", test_retain_batch_set_then_clear },
	{ "db_retain_batch_clear_after_flush",
	    test_retain_batch_clear_after_flush },
	{ "db_retain_batch_last_write_wins",
	    test_retain_batch_last_write_wins },
	{ "db_retain_batch_count_trigger",
	    test_retain_batch_count_trigger },
	{ "db_retain_batch_disabled", test_retain_batch_disabled_is_synchronous },
	{ "db_retain_batch_concurrent_topics",
	    test_retain_batch_concurrent_topics },
	{ "db_retain_batch_concurrent_same_topic",
	    test_retain_batch_concurrent_same_topic },
	{ "db_retain_batch_second_db_ignored",
	    test_retain_batch_second_db_ignored },
	{ "db_retain_batch_reconfigured", test_retain_batch_reconfigured },
	{ "db_retain_batch_runtime_disable_refused",
	    test_retain_batch_runtime_disable_refused },
	{ "db_retain_batch_after_shutdown",
	    test_retain_batch_after_shutdown },
	{ "db_retain_batch_shutdown_race",
	    test_retain_batch_shutdown_race },
	{ NULL, NULL },
};
