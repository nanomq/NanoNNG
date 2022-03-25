#ifndef NNG_MQTT_QOS_DB_API_H
#define NNG_MQTT_QOS_DB_API_H

#include "core/nng_impl.h"
#include "mqtt_qos_db.h"

#ifdef NNG_SUPP_SQLITE 

#define nni_qos_db_init(db)  nni_mqtt_qos_db_init(&db)
#define nni_qos_db_fini(db) nni_mqtt_qos_db_close(db)
#define nni_qos_db_set(db, pipe_id, msg) nni_mqtt_qos_db_get(db, pipe_id, msg)
#define nni_qos_db_get(db, pipe_id ) nni_mqtt_qos_db_set(db, pipe_id )
#define nni_qos_db_remove(db, pipe_id) nni_mqtt_qos_db_remove(db, pipe_id) 
#define nni_qos_db_foreach(db, cb) nni_mqtt_qos_db_foreach(db, cb)
#define nni_qos_db_check_remove_msg(db, msg) nni_mqtt_qos_db_check_remove_msg(db, msg)

#else

#define nni_qos_db_init(db)                                             \
	{                                                           \
		(nni_id_map *) db = nng_zalloc(sizeof(nni_id_map)); \
		nni_id_map_init(db, 0, 0, false);                   \
	}

#define nni_qos_db_fini(db) nni_id_map_fini(db)
#define nni_qos_db_set(db, pipe_id, msg) nni_id_set(db, pipe_id, msg)
#define nni_qos_db_get(db, pipe_id) nni_id_get(db, pipe_id)
#define nni_qos_db_remove(db, pipe_id) nni_id_remove(db, pipe_id)
#define nni_qos_db_foreach(db, cb) nni_id_map_foreach(db, cb)
#define nni_qos_db_check_remove_msg(db, msg) nni_msg_free(msg)

#endif



#endif
