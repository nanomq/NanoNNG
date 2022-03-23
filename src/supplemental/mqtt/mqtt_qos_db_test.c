#include <string.h>

#include "mqtt_qos_db.h"
#include "nng/nng.h"
#include "nuts.h"

#define DB_NAME "qos_db.db"

void
test_db_init(void)
{
	sqlite3 *db = NULL;
	TEST_CHECK(nni_mqtt_qos_db_init(&db, DB_NAME) == SQLITE_OK);
	TEST_CHECK(nni_mqtt_qos_db_close(db) == SQLITE_OK);
}

void
test_qos_db_set(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME);

	char *   header = "header002";
	char *   body   = "data002";
	nni_time ts     = 1648004331;

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));
	nni_msg_set_timestamp(msg, ts);

	uint32_t pipe_id = 1000;

	TEST_CHECK(nni_mqtt_qos_db_set(db, pipe_id, 0, msg) == SQLITE_OK);
	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_get(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME);

	char *   header = "header002";
	char *   body   = "data002";
	nni_time ts     = 1648004331;

	uint32_t pipe_id = 1000;

	nni_msg *msg = nni_mqtt_qos_db_get(db, pipe_id);

	TEST_CHECK(strncmp(header, nni_msg_header(msg),
	               nni_msg_header_len(msg)) == 0);
	TEST_CHECK(strncmp(body, nni_msg_body(msg), nni_msg_len(msg)) == 0);
	TEST_CHECK(nni_msg_get_timestamp(msg) == ts);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_remove(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME);

	TEST_CHECK(nni_mqtt_qos_db_remove(db, 1000) == SQLITE_OK);

	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_check_remove_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME);

	char *header = "header002";
	char *body   = "data002";

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));

	TEST_CHECK(nni_mqtt_qos_db_check_remove_msg(db, msg) == SQLITE_OK);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_set(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME);
	nni_mqtt_qos_db_set_pipe(db, 1000, "nanomq-client-999");
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_remove(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME);
	nni_mqtt_qos_db_remove_pipe(db, 1000);
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_update_all(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME);
	nni_mqtt_qos_db_update_all_pipe(db, 0);
	nni_mqtt_qos_db_close(db);
}

TEST_LIST = {
	{ "db_init", test_db_init },
	{ "db_pipe_set", test_pipe_set },
	{ "db_set", test_qos_db_set },
	{ "db_get", test_qos_db_get },
	{ "db_remove", test_qos_db_remove },
	{ "db_check_remove_msg", test_qos_db_check_remove_msg },
	{ "db_pipe_remove", test_pipe_remove },
	{ NULL, NULL },
};