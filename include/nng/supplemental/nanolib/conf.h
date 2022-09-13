#ifndef CONF_H
#define CONF_H

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nng/nng.h"
#include "rule.h"

#define PID_PATH_NAME "/tmp/nanomq/nanomq.pid"
#define CONF_PATH_NAME "/etc/nanomq.conf"
#define CONF_GATEWAY_PATH_NAME "/etc/nanomq_gateway.conf"

#define CONF_TCP_URL_DEFAULT "nmq-tcp://0.0.0.0:1883"
#define CONF_TLS_URL_DEFAULT "tls+nmq-tcp://0.0.0.0:8883"
#define CONF_WS_URL_DEFAULT "nmq-ws://0.0.0.0:8083/mqtt"
#define CONF_WSS_URL_DEFAULT "nmq-wss://0.0.0.0:8084/mqtt"

#define BROKER_NMQ_TCP_URL_PREFIX "nmq-tcp"
#define BROKER_NMQ_TCP_TLS_URL_PREFIX "tls+nmq-tcp"
#define BROKER_NMQ_WS_URL_PREFIX "nmq-ws"
#define BROKER_NMQ_WSS_URL_PREFIX "nmq-wss"

#define BROKER_TCP_URL_PREFIX "broker+tcp"
#define BROKER_WS_URL_PREFIX "nmq+ws"
#define BROKER_WSS_URL_PREFIX "nmq+wss"

#define RULE_ENG_OFF 0
#define RULE_ENG_SDB 1
#define RULE_ENG_FDB (1 << 1)
#define RULE_ENG_MDB (1 << 2)
#define RULE_ENG_RPB (1 << 3)


#define FREE_NONULL(p)    \
	if (p) {          \
		free(p);  \
		p = NULL; \
	}

// log type
#define LOG_TO_FILE (1 << 0)
#define LOG_TO_CONSOLE (1 << 1)
#define LOG_TO_SYSLOG (1 << 2)

struct conf_log {
	uint8_t type;
	int     level;
	char *  dir;
	char *  file;
	FILE *  fp;
	char *  abs_path;        // absolut path of log file
	char *  rotation_sz_str; // 1000KB, 100MB, 10GB
	size_t  rotation_sz;     // unit: byte
	size_t  rotation_count;  // rotation count
};

typedef struct conf_log conf_log;

struct conf_auth {
	size_t count;
	char **usernames;
	char **passwords;
};
typedef struct conf_auth conf_auth;

struct conf_tls {
	bool  enable;
	char *url; // "tls+nmq-tcp://addr:port"
	char *cafile;
	char *certfile;
	char *keyfile;
	char *ca;
	char *cert;
	char *key;
	char *key_password;
	bool  verify_peer;
	bool  set_fail; // fail_if_no_peer_cert
};

typedef struct conf_tls conf_tls;

struct conf_sqlite {
	bool   enable;
	size_t disk_cache_size;   // specify the max rows of sqlite table
	char * mounted_file_path; // specify the db file path
	size_t
	    flush_mem_threshold; // flush to sqlite table when count of message
	                         // is equal or greater than this value
	uint64_t resend_interval; // resend caching message interval (ms)
};

typedef struct conf_sqlite conf_sqlite;

struct conf_http_header {
	char *key;
	char *value;
};

typedef struct conf_http_header conf_http_header;

typedef enum {
	ACCESS,
	USERNAME,
	CLIENTID,
	IPADDRESS,
	PROTOCOL,
	PASSWORD,
	SOCKPORT,    // sockport of server accepted
	COMMON_NAME, // common name of client TLS cert
	SUBJECT,     // subject of client TLS cert
	TOPIC,
	MOUNTPOINT,
} http_param_type;

struct conf_http_param {
	char *          name;
	http_param_type type;
};

typedef struct conf_http_param conf_http_param;

struct conf_auth_http_req {
	char *url;
	char *method;
	size_t header_count;
	conf_http_header **headers;
	size_t param_count;
	conf_http_param **params;
};

typedef struct conf_auth_http_req conf_auth_http_req;

struct conf_auth_http {
	bool               enable;
	conf_auth_http_req auth_req;
	conf_auth_http_req super_req;
	conf_auth_http_req acl_req;
	uint64_t           timeout;         // seconds
	uint64_t           connect_timeout; // seconds
	size_t             pool_size;
	// TODO not support yet
	conf_tls tls;
};

typedef struct conf_auth_http conf_auth_http;

struct conf_jwt {
	char *iss;
	char *public_keyfile;
	char *private_keyfile;
	char *public_key;
	char *private_key;
	size_t public_key_len;
	size_t private_key_len;
};

typedef struct conf_jwt conf_jwt;

typedef enum {
	BASIC,
	JWT,
	NONE_AUTH,
} auth_type;

struct conf_http_server {
	bool      enable;
	uint16_t  port;
	char *    username;
	char *    password;
	size_t    parallel;
	auth_type auth_type;
	conf_jwt  jwt;
};

typedef struct conf_http_server conf_http_server;

struct conf_websocket {
	bool  enable;
	char *url;     // "nmq-ws://addr:port/path"
	char *tls_url; // "nmq-wss://addr:port/path"
};

typedef struct conf_websocket conf_websocket;

typedef struct {
	char *   topic;
	uint32_t topic_len;
	uint8_t  qos;
} subscribe;

struct conf_bridge_node {
	bool         enable;
	char *       name;
	char *       address;
	char *       host;
	uint16_t     port;
	uint8_t      proto_ver;
	char *       clientid;
	bool         clean_start;
	char *       username;
	char *       password;
	uint16_t     keepalive;
	uint64_t     qkeepalive;		//keepalive timeout interval of QUIC transport
	size_t       forwards_count;
	char **      forwards;
	size_t       sub_count;
	subscribe *  sub_list;
	uint64_t     parallel;
	conf_tls     tls;
	void *       sock;
	conf_sqlite *sqlite;
};

typedef struct conf_bridge_node conf_bridge_node;

struct conf_bridge {
	size_t count;
	conf_bridge_node **nodes;
	conf_sqlite sqlite;
};

typedef struct conf_bridge conf_bridge;

typedef struct {
    const char *zmq_sub_url;
    const char *zmq_pub_url;
    const char *mqtt_url;
    const char *sub_topic;
    const char *pub_topic;
    const char *zmq_sub_pre;
    const char *zmq_pub_pre;
    const char *path;
    const char *username;
    const char *password;
    void       *zmq_sender;
    int         proto_ver;
    int         keepalive;
    bool        clean_start;
    int         parallel;
    enum {PUB_SUB, REQ_REP} type;
} zmq_gateway_conf;


typedef enum {
	CLIENT_CONNECT,
	CLIENT_CONNACK,
	CLIENT_CONNECTED,
	CLIENT_DISCONNECTED,
	CLIENT_SUBSCRIBE,
	CLIENT_UNSUBSCRIBE,
	SESSION_SUBSCRIBED,
	SESSION_UNSUBSCRIBED,
	SESSION_TERMINATED,
	MESSAGE_PUBLISH,
	MESSAGE_DELIVERED,
	MESSAGE_ACKED,
	UNKNOWN_EVENT,
} webhook_event;

typedef enum {
	plain,
	base64,
	base62
} hook_payload_type;

struct conf_web_hook_rule {
	uint16_t      rule_num;
	webhook_event event;
	char *        action;
	char *        topic;
};

typedef struct conf_web_hook_rule conf_web_hook_rule;

struct conf_web_hook {
	bool   enable;
	char * url;
	size_t pool_size;
	hook_payload_type encode_payload;
	size_t header_count;
	conf_http_header **headers;

	uint16_t            rule_count;
	conf_web_hook_rule **rules;

	// TODO not support yet
	conf_tls tls;
};

typedef struct conf_web_hook  conf_web_hook;

typedef enum {
	memory,
	sqlite,
} persistence_type;

struct conf {
	char *   conf_file;
	char *   url; // "nmq-tcp://addr:port"
	int      num_taskq_thread;
	int      max_taskq_thread;
	int      property_size;
	int      msq_len;
	uint32_t parallel;
	uint32_t max_packet_size;
	uint32_t client_max_packet_size;
	uint32_t qos_duration;
	float    backoff;
	void *   db_root;
	bool     allow_anonymous;
	bool     daemon;
	bool     bridge_mode;

	conf_log         log;
	conf_sqlite      sqlite;
	conf_tls         tls;
	conf_http_server http_server;
	conf_websocket   websocket;
	conf_bridge      bridge;
	conf_bridge      aws_bridge;
	conf_web_hook    web_hook;

#if defined(SUPP_RULE_ENGINE)
	conf_rule rule_eng;
#endif

	conf_auth         auths;
	conf_auth_http    auth_http;
	struct hashmap_s *cid_table;
};

typedef struct conf conf;

extern void conf_parse(conf *nanomq_conf);
extern void conf_init(conf *nanomq_conf);
extern void print_conf(conf *nanomq_conf);
extern void conf_fini(conf *nanomq_conf);
extern void conf_update(const char *fpath, const char *key, char *value);
extern void conf_update2(const char *fpath, const char *key1, const char *key2,
    const char *key3, char *value);
NNG_DECL void conf_update_var(
    const char *fpath, const char *key, uint8_t type, void *var);
NNG_DECL void conf_update_var2(const char *fpath, const char *key1,
    const char *key2, const char *key3, uint8_t type, void *var);

#define conf_update_int(path, key, var) \
	conf_update_var(path, key, 0, (void *) &(var))
#define conf_update_u8(path, key, var) \
	conf_update_var(path, key, 1, (void *) &(var))
#define conf_update_u16(path, key, var) \
	conf_update_var(path, key, 2, (void *) &(var))
#define conf_update_u32(path, key, var) \
	conf_update_var(path, key, 3, (void *) &(var))
#define conf_update_u64(path, key, var) \
	conf_update_var(path, key, 4, (void *) &(var))
#define conf_update_long(path, key, var) \
	conf_update_var(path, key, 5, (void *) &(var))
#define conf_update_double(path, key, var) \
	conf_update_var(path, key, 6, (void *) &(var))
#define conf_update_bool(path, key, var) \
	conf_update_var(path, key, 7, (void *) &(var))

#define conf_update2_int(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 0, (void *) &(var))
#define conf_update2_u8(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 1, (void *) &(var))
#define conf_update2_u16(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 2, (void *) &(var))
#define conf_update2_u32(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 3, (void *) &(var))
#define conf_update2_u64(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 4, (void *) &(var))
#define conf_update2_long(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 5, (void *) &(var))
#define conf_update2_double(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 6, (void *) &(var))
#define conf_update2_bool(path, key1, key2, key3, var) \
	conf_update_var2(path, key1, key2, key3, 7, (void *) &(var))

#endif
