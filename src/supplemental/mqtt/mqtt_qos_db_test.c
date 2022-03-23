#include <string.h>

#include "mqtt_qos_db.h"
#include "nng/nng.h"
#include "nuts.h"

#define DB_NAME "qos_db.db"

void
test_db_init(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_set(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db);

	char *   header = "header002";
	char *   body   = "data002";
	nni_time ts     = 1648004331;

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));
	nni_msg_set_timestamp(msg, ts);

	uint32_t pipe_id = 1000;
	uint8_t qos = 2;
	msg = MQTT_DB_PACKED_MSG_QOS(msg, qos);
	nni_mqtt_qos_db_set(db, pipe_id, msg);
	msg = MQTT_DB_GET_MSG_POINTER(msg);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_get(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db);

	char *   header = "header002";
	char *   body   = "data002";
	nni_time ts     = 1648004331;

	uint32_t pipe_id = 1000;

	nni_msg *msg = nni_mqtt_qos_db_get(db, pipe_id);
	TEST_CHECK(MQTT_DB_GET_QOS_BITS(msg) == 2);
	//be careful nni_msg had been changed before executing nni_mqtt_qos_db_get();
	msg = MQTT_DB_GET_MSG_POINTER(msg);
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
	nni_mqtt_qos_db_init(&db);
	nni_mqtt_qos_db_remove(db, 1000);
	nni_mqtt_qos_db_close(db);
}

void
test_qos_db_check_remove_msg(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db);

	char *header = "header002";
	char *body   = "data002";

	nni_msg *msg;
	nni_msg_alloc(&msg, 0);
	nni_msg_header_append(msg, header, strlen(header));
	nni_msg_append(msg, body, strlen(body));

	nni_mqtt_qos_db_check_remove_msg(db, msg);

	nni_msg_free(msg);
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_set(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db);
	nni_mqtt_qos_db_set_pipe(db, 1000, "nanomq-client-999");
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_remove(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db);
	nni_mqtt_qos_db_remove_pipe(db, 1000);
	nni_mqtt_qos_db_close(db);
}

void
test_pipe_update_all(void)
{
	sqlite3 *db = NULL;
	nni_mqtt_qos_db_init(&db);
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