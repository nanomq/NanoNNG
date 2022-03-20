#include <string.h>

#include "mqtt_qos_db.h"
#include "nng/nng.h"
#include "nuts.h"

#define DB_NAME "qos_db.db"
#define TABLE_NAME "qos_table"

void
test_db_init(void)
{
	sqlite3 *db = NULL;
	TEST_CHECK(
	    nni_mqtt_qos_db_init(&db, DB_NAME, TABLE_NAME) == SQLITE_OK);
	TEST_CHECK(nni_mqtt_qos_db_close(db) == SQLITE_OK);
}

void
test_qos_db_set(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME, TABLE_NAME);

	char *header = "header001";
	char *body   = "data001";

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));

	uint32_t pipe_id = 1000;

	TEST_CHECK(nni_mqtt_qos_db_set(db, pipe_id, msg) == SQLITE_OK);
	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_get(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME, TABLE_NAME);

	char *header = "header001";
	char *body   = "data001";

	uint32_t pipe_id = 1000;

	nni_msg *msg = nni_mqtt_qos_db_get(db, pipe_id);

	TEST_CHECK(strncmp(header, nni_msg_header(msg),
	               nni_msg_header_len(msg)) == 0);
	TEST_CHECK(strncmp(body, nni_msg_body(msg), nni_msg_len(msg)) == 0);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_remove(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db, DB_NAME, TABLE_NAME);

	TEST_CHECK(nni_mqtt_qos_db_remove(db, 1000));

	nni_mqtt_qos_db_close(db);
}

TEST_LIST = {
	{ "db_init", test_db_init },
	{ "qos_db_set", test_qos_db_set },
	{ "qos_db_get", test_qos_db_get },
	{ "qos_db_remove", test_qos_db_remove },
	{ NULL, NULL },
};