#include "mqtt_qos_db_api.h"
#ifdef NNG_HAVE_MQTT_BROKER
#include "nng/protocol/mqtt/mqtt_parser.h"
#endif

void
nni_qos_db_set(persistence_type type, void *db, uint32_t pipe_id,
    uint16_t packet_id, nng_msg *msg)
{
	switch (type) {
	case sqlite:
#if defined(NNG_SUPP_SQLITE) && defined(NNG_HAVE_MQTT_BROKER)
		nni_mqtt_qos_db_set((sqlite3 *) (db), pipe_id, packet_id, msg);
		nni_msg_free(NANO_NNI_LMQ_GET_MSG_POINTER(msg));
#else
		NNI_ARG_UNUSED(pipe_id);
		NNI_ARG_UNUSED(packet_id);
		NNI_ARG_UNUSED(msg);
#endif
		break;
	case memory:
		nni_id_set((nni_id_map *) (db), packet_id, msg);
		break;
	default:
		break;
	}
}

nng_msg *
nni_qos_db_get(
    persistence_type type, void *db, uint32_t pipe_id, uint16_t packet_id)
{
	nng_msg *msg = NULL;
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		msg =
		    nni_mqtt_qos_db_get((sqlite3 *) (db), pipe_id, packet_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(pipe_id);
		msg = nni_id_get((nni_id_map *) (db), packet_id);
		break;
	default:
		break;
	}
	return msg;
}

nng_msg *
nni_qos_db_get_one(
    persistence_type type, void *db, uint32_t pipe_id, uint16_t *packet_id)
{
	nng_msg *msg = NULL;
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		msg = nni_mqtt_qos_db_get_one(
		    (sqlite3 *) (db), pipe_id, (uint16_t *) packet_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(pipe_id);
		msg = nni_id_get_any((nni_id_map *) (db), packet_id);
		break;
	default:
		break;
	}
	return msg;
}

void
nni_qos_db_remove(
    persistence_type type, void *db, uint32_t pipe_id, uint16_t packet_id)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove((sqlite3 *) (db), pipe_id, packet_id);
		break;
#endif
	case memory:
		NNI_ARG_UNUSED(pipe_id);
		nni_id_remove((nni_id_map *) (db), packet_id);
		break;
	default:
		break;
	}
}

void
nni_qos_db_remove_by_pipe(persistence_type type, void *db, uint32_t pipe_id)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_by_pipe((sqlite3 *) (db), pipe_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(pipe_id);
		break;
	default:
		break;
	}
}

void
nni_qos_db_remove_msg(persistence_type type, void *db, nng_msg *msg)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_msg((sqlite3 *) (db), msg);
		nni_msg_free(msg);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(msg);
		break;
	default:
		break;
	}
}

void
nni_qos_db_remove_unused_msg(persistence_type type, void *db)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_unused_msg((sqlite3 *) (db));
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		break;
	default:
		break;
	}
}

void
nni_qos_db_remove_all_msg(persistence_type type, void *db, nni_idhash_cb cb)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_all_msg((sqlite3 *) (db));
#endif
		break;
	case memory:
		nni_id_map_foreach((nni_id_map *) (db), cb);
		break;
	default:
		break;
	}
}

void
nni_qos_db_reset_pipe(persistence_type type, void *db)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_update_all_pipe((sqlite3 *) (db), 0);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		break;
	default:
		break;
	}
}

void
nni_qos_db_set_pipe(
    persistence_type type, void *db, uint32_t pipe_id, const char *client_id)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_set_pipe((sqlite3 *) db, pipe_id, client_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(pipe_id);
		NNI_ARG_UNUSED(client_id);
		break;
	default:
		break;
	}
}

void
nni_qos_db_remove_pipe(persistence_type type, void *db, uint32_t pipe_id)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_pipe((sqlite3 *) db, pipe_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(pipe_id);
		break;
	default:
		break;
	}
}

int
nni_qos_db_set_client_msg(persistence_type type, void *db, uint32_t pipe_id,
    uint16_t packet_id, nng_msg *msg)
{
	int rv = 0;
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		rv = nni_mqtt_qos_db_set_client_msg(
		    (sqlite3 *) db, pipe_id, packet_id, msg);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(pipe_id);
		rv = nni_id_set((nni_id_map *) &db, packet_id, msg);
		break;
	default:
		break;
	}
	return rv;
}

nng_msg *
nni_qos_db_get_client_msg(
    persistence_type type, void *db, uint32_t pipe_id, uint16_t packet_id)
{
	nng_msg *msg = NULL;
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		msg = nni_mqtt_qos_db_get_client_msg(
		    (sqlite3 *) db, pipe_id, packet_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(pipe_id);
		msg = nni_id_get((nni_id_map *) &db, packet_id);
		break;
	default:
		break;
	}
	return msg;
}

void
nni_qos_db_remove_client_msg(
    persistence_type type, void *db, uint32_t pipe_id, uint16_t packet_id)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_client_msg(
		    (sqlite3 *) db, pipe_id, packet_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(pipe_id);
		nni_id_remove((nni_id_map *) &db, packet_id);
		break;
	default:
		break;
	}
}

void
nni_qos_db_remove_client_msg_by_id(
    persistence_type type, void *db, uint64_t row_id)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_remove_client_msg_by_id(
		    (sqlite3 *) db, row_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		NNI_ARG_UNUSED(row_id);
		break;
	default:
		break;
	}
}

nng_msg *
nni_qos_db_get_one_client_msg(
    persistence_type type, void *db, uint64_t *row_id, uint16_t *packet_id)
{
	nng_msg *msg = NULL;
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		msg = nni_mqtt_qos_db_get_one_client_msg(
		    (sqlite3 *) db, row_id, packet_id);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(row_id);
		msg = nni_id_get_any((nni_id_map *) db, packet_id);
		break;
	default:
		break;
	}
	return msg;
}

void
nni_qos_db_reset_client_msg_pipe_id(persistence_type type, void *db)
{
	switch (type) {
	case sqlite:
#ifdef NNG_SUPP_SQLITE
		nni_mqtt_qos_db_reset_client_msg_pipe_id((sqlite3 *) db);
#endif
		break;
	case memory:
		NNI_ARG_UNUSED(db);
		break;
	default:
		break;
	}
}
