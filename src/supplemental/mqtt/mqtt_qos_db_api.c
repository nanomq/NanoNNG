#include "mqtt_qos_db_api.h"
#ifdef NNG_HAVE_MQTT_BROKER
#include "nng/protocol/mqtt/mqtt_parser.h"
#endif

void
nni_qos_db_set(bool is_sqlite, void *db, uint32_t pipe_id, uint16_t packet_id,
    nng_msg *msg)
{
	if (is_sqlite) {
#if defined(NNG_SUPP_SQLITE) && defined(NNG_HAVE_MQTT_BROKER)
		nni_mqtt_qos_db_set((sqlite3 *) (db), pipe_id, packet_id, msg);
		nni_msg_free(msg);
#else
		NNI_ARG_UNUSED(pipe_id);
		NNI_ARG_UNUSED(packet_id);
		NNI_ARG_UNUSED(msg);
#endif
	} else {
		nni_id_set((nni_id_map *) (db), packet_id, msg);
	}
}

nng_msg *
nni_qos_db_get(bool is_sqlite, void *db, uint32_t pipe_id, uint16_t packet_id)
{
	nng_msg *msg = NULL;
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		msg =
		    nni_mqtt_qos_db_get((sqlite3 *) (db), pipe_id, packet_id);
#endif
	} else {
		NNI_ARG_UNUSED(pipe_id);
		msg = nni_id_get((nni_id_map *) (db), packet_id);
	}
	return msg;
}

nng_msg *
nni_qos_db_get_one(
    bool is_sqlite, void *db, uint32_t pipe_id, uint16_t *packet_id)
{
	nng_msg *msg = NULL;
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		msg = nni_mqtt_qos_db_get_one(
		    (sqlite3 *) (db), pipe_id, (uint16_t *) packet_id);
#endif
	} else {
		NNI_ARG_UNUSED(pipe_id);
		msg = nni_id_get_min((nni_id_map *) (db), packet_id);
	}
	return msg;
}

void
nni_qos_db_remove(
    bool is_sqlite, void *db, uint32_t pipe_id, uint16_t packet_id)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove((sqlite3 *) (db), pipe_id, packet_id);
#endif
	} else {
		NNI_ARG_UNUSED(pipe_id);
		nni_id_remove((nni_id_map *) (db), packet_id);
	}
}

void
nni_qos_db_remove_oldest(bool is_sqlite, void *db, uint64_t limit)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_oldest((sqlite3 *) (db), limit);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(limit);
	}
}

void
nni_qos_db_remove_by_pipe(bool is_sqlite, void *db, uint32_t pipe_id)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_by_pipe((sqlite3 *) (db), pipe_id);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(pipe_id);
	}
}

void
nni_qos_db_remove_msg(bool is_sqlite, void *db, nng_msg *msg)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_msg((sqlite3 *) (db), msg);
		nni_msg_free(msg);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		nni_msg_free(msg);
	}
}

void
nni_qos_db_remove_unused_msg(bool is_sqlite, void *db)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_unused_msg((sqlite3 *) (db));
#endif
	} else {
		NNI_ARG_UNUSED(db);
	}
}

void
nni_qos_db_remove_all_msg(bool is_sqlite, void *db, nni_idhash_cb cb)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_all_msg((sqlite3 *) (db));
#endif
	} else {
		nni_id_map_foreach((nni_id_map *) (db), cb);
	}
}

void
nni_qos_db_reset_pipe(bool is_sqlite, void *db)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_update_all_pipe((sqlite3 *) (db), 0);
#endif
	} else {
		NNI_ARG_UNUSED(db);
	}
}

void
nni_qos_db_set_pipe(
    bool is_sqlite, void *db, uint32_t pipe_id, const char *client_id)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_set_pipe((sqlite3 *) db, pipe_id, client_id);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(pipe_id);
		NNI_ARG_UNUSED(client_id);
	}
}

void
nni_qos_db_remove_pipe(bool is_sqlite, void *db, uint32_t pipe_id)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_pipe((sqlite3 *) db, pipe_id);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(pipe_id);
	}
}

int
nni_qos_db_set_client_msg(bool is_sqlite, void *db, uint32_t pipe_id,
    uint16_t packet_id, nng_msg *msg, const char *config_name,
    uint8_t proto_ver)
{
	int rv = 0;
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		rv = nni_mqtt_qos_db_set_client_msg((sqlite3 *) db, pipe_id,
		    packet_id, msg, config_name, proto_ver);
#endif
	} else {
		rv = nni_id_set((nni_id_map *) db, packet_id, msg);
		NNI_ARG_UNUSED(pipe_id);
		NNI_ARG_UNUSED(config_name);
	}
	NNI_ARG_UNUSED(proto_ver);
	return rv;
}

nng_msg *
nni_qos_db_get_client_msg(bool is_sqlite, void *db, uint32_t pipe_id,
    uint16_t packet_id, const char *config_name)
{
	nng_msg *msg = NULL;
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		msg = nni_mqtt_qos_db_get_client_msg(
		    (sqlite3 *) db, pipe_id, packet_id, config_name);
#endif
	} else {
		msg = nni_id_get((nni_id_map *) db, packet_id);
		NNI_ARG_UNUSED(pipe_id);
		NNI_ARG_UNUSED(config_name);
	}
	return msg;
}

void
nni_qos_db_remove_client_msg(
    bool is_sqlite, void *db, uint32_t pipe_id, uint16_t packet_id, const char *config_name)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_client_msg(
		    (sqlite3 *) db, pipe_id, packet_id, config_name);
#endif
	} else {
		nni_id_remove((nni_id_map *) db, packet_id);
		NNI_ARG_UNUSED(pipe_id);
		NNI_ARG_UNUSED(config_name);
	}
}

void
nni_qos_db_remove_oldest_client_msg(
    bool is_sqlite, void *db, uint64_t limit, const char *config_name)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_oldest_client_msg(
		    (sqlite3 *) (db), limit, config_name);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(limit);
		NNI_ARG_UNUSED(config_name);
	}
}

void
nni_qos_db_remove_client_msg_by_id(bool is_sqlite, void *db, uint64_t row_id)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_client_msg_by_id(
		    (sqlite3 *) db, row_id);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(row_id);
	}
}

nng_msg *
nni_qos_db_get_one_client_msg(bool is_sqlite, void *db, uint64_t *row_id,
    uint16_t *packet_id, const char *config_name)
{
	nng_msg *msg = NULL;
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		msg = nni_mqtt_qos_db_get_one_client_msg(
		    (sqlite3 *) db, row_id, packet_id, config_name);
#endif
	} else {
		NNI_ARG_UNUSED(row_id);
		NNI_ARG_UNUSED(config_name);
		msg = nni_id_get_min((nni_id_map *) db, packet_id);
	}
	return msg;
}

void
nni_qos_db_reset_client_msg_pipe_id(
    bool is_sqlite, void *db, const char *config_name)
{
	if (is_sqlite) {
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_reset_client_msg_pipe_id(
		    (sqlite3 *) db, config_name);
#endif
	} else {
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(config_name);
	}
}
