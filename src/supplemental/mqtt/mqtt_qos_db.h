#ifndef NNG_MQTT_QOS_DB_H
#define NNG_MQTT_QOS_DB_H

#include "nng/nng.h"
#include "core/nng_impl.h"
#include "supplemental/sqlite/sqlite3.h"

NNG_DECL int nni_mqtt_qos_db_init(
    sqlite3 **db, const char *db_name, const char *table);
NNG_DECL int nni_mqtt_qos_db_close(sqlite3 *db);
NNG_DECL int nni_mqtt_qos_db_set(sqlite3 *db, uint32_t id, nni_msg *msg);
NNG_DECL nni_msg *nni_mqtt_qos_db_get(sqlite3 *db, uint32_t id);
NNG_DECL int      nni_mqtt_qos_db_remove(sqlite3 *db, uint32_t id);
#endif