#include "sqlite_handler.h"
#include "nng/mqtt/mqtt_client.h"

#if defined(NNG_SUPP_SQLITE)

inline bool
sqlite_is_enabled(nni_mqtt_sqlite_option *sqlite)
{
	if (sqlite != NULL && sqlite->sqlite_conf != NULL) {
		return sqlite->sqlite_conf->enable;
	}
	return false;
}

inline nni_msg *
sqlite_get_cache_msg(nni_mqtt_sqlite_option *sqlite)
{
	nni_msg *msg    = NULL;
	int64_t  row_id = 0;

	msg = nni_mqtt_qos_db_get_client_offline_msg(
	    sqlite->db, &row_id, sqlite->bridge_name);
	if (msg != NULL) {
		nni_mqtt_qos_db_remove_client_offline_msg(sqlite->db, row_id);
	}

	return msg;
}

inline void
sqlite_flush_lmq(nni_mqtt_sqlite_option *sqlite, nni_lmq *lmq)
{
	if (sqlite_is_enabled(sqlite)) {
		nni_mqtt_qos_db_set_client_offline_msg_batch(sqlite->db, lmq,
		    sqlite->bridge_name, sqlite->proto_ver);
		nni_mqtt_qos_db_remove_oldest_client_offline_msg(sqlite->db,
		    sqlite->sqlite_conf->disk_cache_size,
		    sqlite->bridge_name);
	}
}

inline void
sqlite_flush_offline_cache(nni_mqtt_sqlite_option *sqlite)
{
	sqlite_flush_lmq(sqlite, &sqlite->offline_cache);
}

#else

inline bool
sqlite_is_enabled(nni_mqtt_sqlite_option *sqlite)
{
	NNI_ARG_UNUSED(sqlite);
	return false;
}

inline nni_msg *
sqlite_get_cache_msg(nni_mqtt_sqlite_option *sqlite)
{
	NNI_ARG_UNUSED(sqlite);
	return NULL;
}

inline void
sqlite_flush_offline_cache(nni_mqtt_sqlite_option *sqlite)
{
	NNI_ARG_UNUSED(sqlite);
}

#endif