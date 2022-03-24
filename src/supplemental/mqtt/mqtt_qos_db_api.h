#ifndef NNG_MQTT_QOS_DB_API_H
#define NNG_MQTT_QOS_DB_API_H

#include "core/nng_impl.h"
#include "mqtt_qos_db.h"

struct nni_db_ops {
	void *db;
	void (*db_init)(void **db);
	void (*db_fini)(void *db);
	void *(*db_get)(void *db, uint32_t);
	void (*db_set)(void *db, uint32_t, void *);
	void (*db_remove)(void *db, uint32_t);
	void (*db_check_remove_msg)(void *db, void *);
	void (*db_foreach)(void *db, nni_idhash_cb);
};

typedef struct nni_db_ops nni_db_ops;

#endif
