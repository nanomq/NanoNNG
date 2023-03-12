#ifndef NNG_MQTT_QOS_DB_H
#define NNG_MQTT_QOS_DB_H

#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/sqlite/sqlite3.h"

#if defined(NNG_HAVE_MQTT_BROKER)
#include "nng/supplemental/nanolib/conf.h"
#endif

/**
 *
 *  pipe_client_table
------------------------------
|  id  | pipe_id | client_id |
------------------------------
|      |         |           |
------------------------------
**/

/**
 *
 *  msg_table
---------------------
|    id   |   data  |
---------------------
|         |         |
---------------------
**/

/**
 * 
 * main_table
-------------------------------------------------
| id | p_id  | packet_id | msg_id | qos  | ts   |
-------------------------------------------------
|    |       |           |        |      |      |
-------------------------------------------------
**/

/**
 *
 *  client msg_table
-----------------------------------------------------------------
| id | pipe_id  | packet_id | data | ts   | info_id | proto_ver |
-----------------------------------------------------------------
|    |          |           |      |      |         |           |
-----------------------------------------------------------------
**/

/**
 *
 * client offline_msg_table
-----------------------------------------------
| id    | data | ts   | info_id   | proto_ver |
-----------------------------------------------
|       |      |      |           |           |
-----------------------------------------------
**/

/**
 * 
 * client info_table
-------------------------------------------------------------------
| id    | config_name | client_id | proto_name  | proto_ver | ts  |
-------------------------------------------------------------------
|       |             |           |             |           |     |
-------------------------------------------------------------------
**/

struct nng_mqtt_sqlite_option {
#if defined(NNG_HAVE_MQTT_BROKER)
	conf_bridge_node *bridge;
#else
	void *bridge;
#endif
	char *  db_name;
	nni_lmq offline_cache;
#if defined(NNG_SUPP_SQLITE)
	sqlite3 *db;
#else
	void *db;
#endif
};

typedef struct nng_mqtt_sqlite_option nni_mqtt_sqlite_option;

#define MQTT_DB_GET_QOS_BITS(msg) ((size_t)(msg) &0x03)
#define MQTT_DB_PACKED_MSG_QOS(msg, qos) \
	((nni_msg *) ((size_t)(msg) | ((qos) &0x03)))

#define MQTT_DB_GET_MSG_POINTER(msg) ((nni_msg *) ((size_t)(msg) & (~0x03)))

extern void nni_mqtt_qos_db_init(sqlite3 **, const char *, const char *, bool);
extern void nni_mqtt_qos_db_close(sqlite3 *);
extern void     nni_mqtt_qos_db_set(sqlite3 *, uint32_t, uint16_t, nni_msg *);
extern nni_msg *nni_mqtt_qos_db_get(sqlite3 *, uint32_t, uint16_t);
extern nni_msg *nni_mqtt_qos_db_get_one(sqlite3 *, uint32_t, uint16_t *);
extern void     nni_mqtt_qos_db_remove(sqlite3 *, uint32_t, uint16_t);
extern void     nni_mqtt_qos_db_remove_oldest(sqlite3 *, uint64_t);
extern void     nni_mqtt_qos_db_remove_by_pipe(sqlite3 *, uint32_t);
extern void     nni_mqtt_qos_db_remove_msg(sqlite3 *, nni_msg *);
extern void     nni_mqtt_qos_db_remove_unused_msg(sqlite3 *);
extern void     nni_mqtt_qos_db_remove_all_msg(sqlite3 *);
extern void     nni_mqtt_qos_db_foreach(sqlite3 *, nni_idhash_cb);
extern void     nni_mqtt_qos_db_set_pipe(sqlite3 *, uint32_t, const char *);
extern void     nni_mqtt_qos_db_insert_pipe(sqlite3 *, uint32_t, const char *);
extern void     nni_mqtt_qos_db_remove_pipe(sqlite3 *, uint32_t);
extern void     nni_mqtt_qos_db_update_pipe_by_clientid(
        sqlite3 *, uint32_t, const char *);
extern void nni_mqtt_qos_db_update_all_pipe(sqlite3 *, uint32_t);
extern void nni_mqtt_qos_db_check_remove_msg(sqlite3 *, nni_msg *);

extern int nni_mqtt_qos_db_set_retain(
    sqlite3 *, const char *, nni_msg *, uint8_t);
extern nni_msg *nni_mqtt_qos_db_get_retain(sqlite3 *, const char *);
extern int      nni_mqtt_qos_db_remove_retain(sqlite3 *, const char *);
extern nni_msg **nni_mqtt_qos_db_find_retain(sqlite3 *, const char *);

extern void nni_mqtt_qos_db_remove_oldest_client_msg(
    sqlite3 *, uint64_t ,const char *);
extern void nni_mqtt_qos_db_remove_oldest_client_offline_msg(
    sqlite3 *, uint64_t ,const char *);
// Only work for client
extern int nni_mqtt_qos_db_set_client_msg(
    sqlite3 *, uint32_t, uint16_t, nni_msg *, const char *, uint8_t);
extern nni_msg *nni_mqtt_qos_db_get_client_msg(
    sqlite3 *, uint32_t, uint16_t, const char *);
extern void nni_mqtt_qos_db_remove_client_msg(
    sqlite3 *, uint32_t, uint16_t, const char *);
extern void nni_mqtt_qos_db_remove_client_msg_by_id(sqlite3 *, uint64_t);
extern nni_msg *nni_mqtt_qos_db_get_one_client_msg(
    sqlite3 *, uint64_t *, uint16_t *, const char *);
extern void nni_mqtt_qos_db_reset_client_msg_pipe_id(sqlite3 *,const char *);

extern int nni_mqtt_qos_db_set_client_offline_msg(
    sqlite3 *, nni_msg *, const char *, uint8_t);
extern int nni_mqtt_qos_db_set_client_offline_msg_batch(
    sqlite3 *, nni_lmq *, const char *, uint8_t);
extern nng_msg *nni_mqtt_qos_db_get_client_offline_msg(sqlite3 *, int64_t *,const char *);
extern int      nni_mqtt_qos_db_remove_client_offline_msg(sqlite3 *, int64_t);
extern int      nni_mqtt_qos_db_remove_all_client_offline_msg(sqlite3 *,const char *);

extern int nni_mqtt_qos_db_set_client_info(
    sqlite3 *, const char *, const char *, const char *, uint8_t);

extern void nni_mqtt_sqlite_db_init(nni_mqtt_sqlite_option *, const char *);
extern void nni_mqtt_sqlite_db_fini(nni_mqtt_sqlite_option *);

#endif