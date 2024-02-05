#include "core/nng_impl.h"

#include "nng/nng.h"
#include "nng/supplemental/nanolib/acl_conf.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/hocon.h"
#include "nng/supplemental/nanolib/log.h"
#include "nng/exchange/exchange.h"
#include "nanolib.h"
#include <ctype.h>
#include <string.h>

typedef struct {
	uint8_t enumerate;
	char *  desc;
} enum_map;
#ifdef ACL_SUPP
static enum_map auth_acl_permit[] = {
	{ ACL_ALLOW, "allow" },
	{ ACL_DENY, "deny" },
	{ -1, NULL },
};

static enum_map auth_deny_action[] = {
	{ ACL_IGNORE, "ignore" },
	{ ACL_DISCONNECT, "disconnect" },
	{ -1, NULL },
};
#endif
static enum_map webhook_encoding[] = {
	{ plain, "plain" },
	{ base64, "base64" },
	{ base62, "base62" },
	{ -1, NULL },
};

static enum_map http_server_auth_type[] = {
	{ BASIC, "basic" },
	{ JWT, "jwt" },
	{ NONE_AUTH, "no_auth" },
	{ -1, NULL },
};

static enum_map compress_type[] = {
	{ UNCOMPRESSED, "uncompressed" },
	{ SNAPPY, "snappy" },
	{ GZIP, "gzip" },
	{ BROTLI, "brotli" },
	{ ZSTD, "zstd" },
	{ LZ4, "lz4" },
	{ -1, NULL },
};

static enum_map encryption_type[] = {
	{AES_GCM_V1, "aes_gcm_v1" },
	{AES_GCM_CTR_V1, "aes_gcm_ctr_v1"}
};

cJSON *hocon_get_obj(char *key, cJSON *jso);

// Read json value into struct
// use same struct fields and json keys
#define hocon_read_str_base(structure, field, key, jso)               \
	do {                                                          \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);       \
		if (NULL == jso_key) {                                \
			log_debug("Config %s is not set, use default!", key);     \
			break;                                        \
		}                                                     \
		if (cJSON_IsString(jso_key)) {                        \
			if (NULL != jso_key->valuestring) {           \
				FREE_NONULL((structure)->field);      \
				(structure)->field =                  \
				    nng_strdup(jso_key->valuestring); \
			}                                             \
		}                                                     \
	} while (0);

char *
compose_url(char *head, char *address)
{
	size_t url_len = strlen(head) + strlen(address) + 1;
	char * url     = nng_alloc(url_len + 1);
	snprintf(url, url_len, "%s%s", head, address);
	return url;
}

#define hocon_read_address_base(structure, field, key, head, jso)            \
	do {                                                                 \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);              \
		if (NULL == jso_key) {                                       \
			log_debug("Config %s is not set, use default!", key);            \
			break;                                               \
		}                                                            \
		if (cJSON_IsString(jso_key)) {                               \
			if (NULL != jso_key->valuestring) {                  \
				FREE_NONULL((structure)->field);             \
				(structure)->field =                         \
				    compose_url(head, jso_key->valuestring); \
			}                                                    \
		}                                                            \
	} while (0);

#define hocon_read_time_base(structure, field, key, jso)                  \
	do {                                                              \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);           \
		if (NULL == jso_key) {                                    \
			log_debug("Config %s is not set, use default!", key);         \
			break;                                            \
		}                                                         \
		if (cJSON_IsString(jso_key)) {                            \
			if (NULL != jso_key->valuestring) {               \
				uint64_t seconds = 0;                     \
				get_time(jso_key->valuestring, &seconds); \
				(structure)->field = seconds;             \
			}                                                 \
		}                                                         \
	} while (0);

#define hocon_read_size_base(structure, field, key, jso)                      \
	do {                                                                  \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);               \
		if (NULL == jso_key) {                                        \
			log_debug("Config %s is not set, use default!", key); \
			break;                                                \
		}                                                             \
		if (cJSON_IsString(jso_key)) {                                \
			if (NULL != jso_key->valuestring) {                   \
				get_size(jso_key->valuestring,                \
				    &(structure)->field);                     \
			}                                                     \
		} else if (cJSON_IsNumber(jso_key) && jso_key->valuedouble) { \
			(structure)->field = (uint64_t) jso_key->valuedouble;   \
		}                                                             \
	} while (0);

#define hocon_read_hex_str_base(structure, field, key, jso)                  \
	do {                                                              \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);           \
		if (NULL == jso_key) {                                    \
			log_debug("Config %s is not set, use default!", key);         \
			break;                                            \
		}                                                         \
		if (cJSON_IsString(jso_key)) {                            \
			if (NULL != jso_key->valuestring) {               \
				uint32_t hex = 0;                     			\
				sscanf(jso_key->valuestring, "%x", &hex); 		\
				(structure)->field = hex;             			\
			}                                                 \
		}                                                         \
	} while (0);


#define hocon_read_bool_base(structure, field, key, jso)            \
	do {                                                        \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);     \
		if (NULL == jso_key) {                              \
			log_debug("Config %s is not set, use default!", key);   \
			break;                                      \
		}                                                   \
		if (cJSON_IsBool(jso_key)) {                        \
			(structure)->field = cJSON_IsTrue(jso_key); \
		}                                                   \
	} while (0);

#define hocon_read_num_base(structure, field, key, jso)                 \
	do {                                                            \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);         \
		if (NULL == jso_key) {                                  \
			log_debug("Config %s is not set, use default!", key);       \
			break;                                          \
		}                                                       \
		if (cJSON_IsNumber(jso_key)) {                          \
			if (jso_key->valuedouble > 0)                      \
				(structure)->field = jso_key->valuedouble; \
		}                                                       \
	} while (0);

#define hocon_read_enum_base(structure, field, key, jso, map)             \
	{                                                                 \
		do {                                                      \
			cJSON *jso_key = hocon_get_obj(key, jso);         \
			char * str     = cJSON_GetStringValue(jso_key);   \
			int    index   = 0;                               \
			if (NULL != str) {                                \
				while (NULL != map[index].desc) {         \
					if (0 ==                          \
					    nni_strcasecmp(               \
					        str, map[index].desc)) {  \
						(structure)->field =      \
						    map[index].enumerate; \
						break;                    \
					}                                 \
					index++;                          \
				}                                         \
			}                                                 \
		} while (0);                                              \
	}

#define hocon_read_str_arr_base(structure, field, key, jso)                  \
	do {                                                                 \
		cJSON *jso_arr = cJSON_GetObjectItem(jso, key);              \
		if (NULL == jso_arr) {                                       \
			log_debug("Config %s is not set, use default!", key);            \
			break;                                               \
		}                                                            \
		cJSON *elem = NULL;                                          \
		cJSON_ArrayForEach(elem, jso_arr)                            \
		{                                                            \
			if (cJSON_IsString(elem)) {                          \
				cvector_push_back((structure)->field,        \
				    nng_strdup(cJSON_GetStringValue(elem))); \
			}                                                    \
		}                                                            \
	} while (0);

// Add easier interface
// if struct fields and json key
// are the same use this interface
#define hocon_read_str(structure, key, jso) \
	hocon_read_str_base(structure, key, #key, jso)
#define hocon_read_time(structure, key, jso) \
	hocon_read_time_base(structure, key, #key, jso)
#define hocon_read_size(structure, key, jso) \
	hocon_read_size_base(structure, key, #key, jso)
#define hocon_read_address(structure, key, head, jso) \
	hocon_read_address_base(structure, key, #key, head, jso)
#define hocon_read_num(structure, key, jso) \
	hocon_read_num_base(structure, key, #key, jso)
#define hocon_read_bool(structure, key, jso) \
	hocon_read_bool_base(structure, key, #key, jso)
#define hocon_read_str_arr(structure, key, jso) \
	hocon_read_str_arr_base(structure, key, #key, jso)
#define hocon_read_enum(structure, key, jso, map) \
	hocon_read_enum_base(structure, key, #key, jso, map)
#define hocon_read_hex_str(structure, key, jso) \
	hocon_read_hex_str_base(structure, key, #key, jso)

static char **
string_split(char *str, char sp)
{
	char **ret = NULL;
	char * p   = str;
	char * p_b = p;
	while (NULL != (p = strchr(p, sp))) {
		// abc.def.jkl
		cvector_push_back(ret, nng_strndup(p_b, p - p_b));
		p_b = ++p;
	}
	cvector_push_back(ret, nng_strdup(p_b));
	return ret;
}

cJSON *
hocon_get_obj(char *key, cJSON *jso)
{
	cJSON *ret     = jso;
	char **str_vec = string_split(key, '.');

	for (size_t i = 0; i < cvector_size(str_vec); i++) {
		ret = cJSON_GetObjectItem(ret, str_vec[i]);
		nng_strfree(str_vec[i]);
	}

	cvector_free(str_vec);

	return ret;
}

static void
conf_http_server_parse_ver2(conf_http_server *http_server, cJSON *json)
{
	// http server
	cJSON *jso_http_server = cJSON_GetObjectItem(json, "http_server");
	if (jso_http_server) {
		http_server->enable = true;
		hocon_read_num(http_server, port, jso_http_server);
		hocon_read_num_base(
		    http_server, parallel, "limit_conn", jso_http_server);
		hocon_read_str(http_server, username, jso_http_server);
		hocon_read_str(http_server, password, jso_http_server);
		hocon_read_enum(http_server, auth_type, jso_http_server,
		    http_server_auth_type);
		conf_jwt *jwt = &(http_server->jwt);

		cJSON *jso_pub_key_file =
		    hocon_get_obj("jwt.public", jso_http_server);
		if (cJSON_IsObject(jso_pub_key_file)) {
			hocon_read_str_base(
			    jwt, public_keyfile, "keyfile", jso_pub_key_file);
			if (file_load_data(jwt->public_keyfile,
			        (void **) &jwt->public_key) > 0) {
				jwt->iss = (char *) nni_plat_file_basename(
				    jwt->public_keyfile);
				jwt->public_key_len = strlen(jwt->public_key);
			}
		}
	}
}

static void
conf_basic_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_sys = cJSON_GetObjectItem(jso, "system");
	if (jso_sys) {
		hocon_read_bool(config, daemon, jso_sys);
		hocon_read_num(config, num_taskq_thread, jso_sys);
		hocon_read_num(config, max_taskq_thread, jso_sys);
		hocon_read_num(config, parallel, jso_sys);
		hocon_read_bool_base(
		    config, ipc_internal, "enable_ipc_internal", jso_sys);
	}

	cJSON *jso_auth = cJSON_GetObjectItem(jso, "auth");
#ifdef ACL_SUPP
	hocon_read_enum_base(
	    config, acl_nomatch, "no_match", jso_auth, auth_acl_permit);
	hocon_read_enum_base(config, acl_deny_action, "deny_action", jso_auth,
	    auth_deny_action);
	hocon_read_bool(config, allow_anonymous, jso_auth);

	cJSON *jso_auth_cache = hocon_get_obj("cache", jso_auth);
	if (jso_auth_cache) {
		config->enable_acl_cache = true;
		hocon_read_num_base(
		    config, acl_cache_max_size, "max_size", jso_auth_cache);
		hocon_read_num_base(config, acl_cache_ttl, "ttl", jso_auth_cache);
	}
#endif
	cJSON *jso_mqtt = hocon_get_obj("mqtt", jso);
	if (jso_mqtt) {
		hocon_read_num(config, property_size, jso_mqtt);
		hocon_read_size(config, max_packet_size, jso_mqtt);
		config->client_max_packet_size = config->max_packet_size;
		hocon_read_num_base(
		    config, msq_len, "max_mqueue_len", jso_mqtt);
		hocon_read_time_base(
		    config, qos_duration, "retry_interval", jso_mqtt);
		hocon_read_num_base(
		    config, backoff, "keepalive_multiplier", jso_mqtt);

		hocon_read_num(config, max_inflight_window, jso_mqtt);
		hocon_read_time(config, max_awaiting_rel, jso_mqtt);
		hocon_read_time(config, await_rel_timeout, jso_mqtt);
	}


	cJSON *jso_listeners = cJSON_GetObjectItem(jso, "listeners");
	if (jso_listeners) {
		cJSON *jso_tcp = cJSON_GetObjectItem(jso_listeners, "tcp");
		if (jso_tcp) {
			hocon_read_address_base(config, url, "bind", "nmq-tcp://", jso_tcp);
			config->enable = true;
			hocon_read_bool_base(config, enable, "enable", jso_tcp);
		} else {
			config->enable = false;
		}

		cJSON *jso_websocket = hocon_get_obj("listeners.ws", jso);
		if (NULL == jso_websocket) {
			log_error("Read config nanomq ws failed!");
			return;
		}

		conf_websocket *websocket = &(config->websocket);
		hocon_read_address_base(
		    websocket, url, "bind", "nmq-ws://", jso_websocket);
		websocket->enable = true;

		conf_tls *tls = &(config->tls);
		if (tls != NULL) {
			cJSON *jso_websocket_tls = hocon_get_obj("listeners.wss", jso);
			if (jso_websocket_tls != NULL) {
				hocon_read_address_base(
				    websocket, tls_url, "bind", "nmq-wss://", jso_websocket_tls);
			}
		}
	}

	conf_http_server_parse_ver2(&(config->http_server), jso);

	return;
}

static void
conf_tls_parse_ver2_base(conf_tls *tls, cJSON *jso_tls)
{
	if (jso_tls) {
		tls->enable = true;
		hocon_read_str(tls, keyfile, jso_tls);
		hocon_read_str(tls, certfile, jso_tls);
		hocon_read_str_base(tls, cafile, "cacertfile", jso_tls);
		hocon_read_str(tls, key_password, jso_tls);

		if (NULL == tls->keyfile ||
		    0 == file_load_data(tls->keyfile, (void **) &tls->key)) {
			log_warn("Read keyfile %s failed!", tls->keyfile);
		}
		if (NULL == tls->certfile ||
		    0 == file_load_data(tls->certfile, (void **) &tls->cert)) {
			log_warn("Read certfile %s failed!", tls->certfile);
		}
		if (NULL == tls->cafile ||
		    0 == file_load_data(tls->cafile, (void **) &tls->ca)) {
			log_error("Read cacertfile %s failed!", tls->cafile);
		}
	}

	return;
}

static void
conf_tcp_parse_ver2_base(conf_tcp *tcp, cJSON *jso_tcp)
{
	if (jso_tcp) {
		tcp->enable = true;
		hocon_read_bool(tcp, nodelay, jso_tcp);
		hocon_read_bool(tcp, keepalive, jso_tcp);
		hocon_read_bool(tcp, quickack, jso_tcp);
		hocon_read_time(tcp, keepidle, jso_tcp);
		hocon_read_time(tcp, keepintvl, jso_tcp);
		hocon_read_time(tcp, keepcnt, jso_tcp);
		hocon_read_time(tcp, sendtimeo, jso_tcp);
		hocon_read_time(tcp, recvtimeo, jso_tcp);
		// TODO: handle error
	}

	return;
}

static void
conf_tls_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_tls = hocon_get_obj("listeners.ssl", jso);
	if (jso_tls) {
		conf_tls *tls = &(config->tls);
		conf_tls_parse_ver2_base(tls, jso_tls);
		hocon_read_bool(tls, verify_peer, jso_tls);
		hocon_read_bool_base(
		    tls, set_fail, "fail_if_no_peer_cert", jso_tls);
		hocon_read_address_base(
		    tls, url, "bind", "tls+nmq-tcp://", jso_tls);
	}

	return;
}

static void
conf_sqlite_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_sqlite = cJSON_GetObjectItem(jso, "sqlite");
	if (jso_sqlite) {
		conf_sqlite *sqlite = &(config->sqlite);
		sqlite->enable = true;
		hocon_read_num(sqlite, disk_cache_size, jso_sqlite);
		hocon_read_str(sqlite, mounted_file_path, jso_sqlite);
		hocon_read_num(sqlite, flush_mem_threshold, jso_sqlite);
		hocon_read_num(sqlite, resend_interval, jso_sqlite);
	}

	return;
}

#if defined(ENABLE_LOG)
static void
conf_log_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_log = cJSON_GetObjectItem(jso, "log");

	if (jso_log) {
		conf_log *log            = &(config->log);
		cJSON    *jso_log_to     = hocon_get_obj("to", jso_log);
		cJSON    *jso_log_to_ele = NULL;
		cJSON_ArrayForEach(jso_log_to_ele, jso_log_to)
		{
			if (!strcmp("file",
			        cJSON_GetStringValue(jso_log_to_ele))) {
				log->type |= LOG_TO_FILE;
			} else if (!strcmp("console",
			               cJSON_GetStringValue(jso_log_to_ele))) {
				log->type |= LOG_TO_CONSOLE;
			} else if (!strcmp("syslog",
			               cJSON_GetStringValue(jso_log_to_ele))) {
				log->type |= LOG_TO_SYSLOG;
			} else {
				log_error("Unsupport log to");
			}
		}

		cJSON *jso_log_level = hocon_get_obj("level", jso_log);
		int    rv = log_level_num(cJSON_GetStringValue(jso_log_level));
		if (-1 != rv) {
			log->level = rv;
		} else {
			log->level = NNG_LOG_ERROR;
		}

		hocon_read_str(log, dir, jso_log);
		hocon_read_str(log, file, jso_log);
		cJSON *jso_log_rotation = hocon_get_obj("rotation", jso_log);
		hocon_read_size_base(
		    log, rotation_sz, "size", jso_log_rotation);
		hocon_read_num_base(
		    log, rotation_count, "count", jso_log_rotation);
	}
	return;
}
#endif

webhook_event
get_webhook_event_ver2(const char *hook_event)
{
	if (nni_strcasecmp("on_client_connect", hook_event) == 0) {
		return CLIENT_CONNECT;
	} else if (nni_strcasecmp("on_client_connack", hook_event) == 0) {
		return CLIENT_CONNACK;
	} else if (nni_strcasecmp("on_client_connected", hook_event) == 0) {
		return CLIENT_CONNECTED;
	} else if (nni_strcasecmp("on_client_disconnected", hook_event) == 0) {
		return CLIENT_DISCONNECTED;
	} else if (nni_strcasecmp("on_client_subscribe", hook_event) == 0) {
		return CLIENT_SUBSCRIBE;
	} else if (nni_strcasecmp("on_client_unsubscribe", hook_event) == 0) {
		return CLIENT_UNSUBSCRIBE;
	} else if (nni_strcasecmp("on_session_subscribed", hook_event) == 0) {
		return SESSION_SUBSCRIBED;
	} else if (nni_strcasecmp("on_session_unsubscribed", hook_event) ==
	    0) {
		return SESSION_UNSUBSCRIBED;
	} else if (nni_strcasecmp("on_session_terminated", hook_event) == 0) {
		return SESSION_TERMINATED;
	} else if (nni_strcasecmp("on_message_publish", hook_event) == 0) {
		return MESSAGE_PUBLISH;
	} else if (nni_strcasecmp("on_message_delivered", hook_event) == 0) {
		return MESSAGE_DELIVERED;
	} else if (nni_strcasecmp("on_message_acked", hook_event) == 0) {
		return MESSAGE_ACKED;
	}
	return UNKNOWN_EVENT;
}

static void
webhook_action_parse_ver2(cJSON *object, conf_web_hook_rule *hook_rule)
{
	cJSON *action = cJSON_GetObjectItem(object, "event");
	if (cJSON_IsString(action)) {
		const char *act_val = cJSON_GetStringValue(action);
		hook_rule->action   = nng_strdup(act_val);
		hook_rule->event    = get_webhook_event_ver2(act_val);
	} else {
		hook_rule->action = NULL;
	}
	cJSON *topic = cJSON_GetObjectItem(object, "topic");
	if (cJSON_IsString(topic)) {
		const char *topic_str = cJSON_GetStringValue(topic);
		hook_rule->topic      = nng_strdup(topic_str);
	} else {
		hook_rule->topic = NULL;
	}
}

static void
conf_web_hook_parse_rules_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_webhook_rules         = hocon_get_obj("webhook.events", jso);
	cJSON *jso_webhook_rule          = NULL;

	conf_web_hook *webhook = &(config->web_hook);
	webhook->rules         = NULL;

	cJSON_ArrayForEach(jso_webhook_rule, jso_webhook_rules)
	{
		conf_web_hook_rule *hook_rule = NNI_ALLOC_STRUCT(hook_rule);
		webhook_action_parse_ver2(jso_webhook_rule, hook_rule);
		cvector_push_back(webhook->rules, hook_rule);
		webhook->rule_count = cvector_size(webhook->rules);
	}

	return;
}

static void
conf_webhook_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_webhook = cJSON_GetObjectItem(jso, "webhook");
	if (jso_webhook) {
		conf_web_hook *webhook = &(config->web_hook);
		webhook->enable = true;
		hocon_read_str(webhook, url, jso_webhook);
		cJSON *webhook_headers = hocon_get_obj("headers", jso_webhook);
		cJSON *webhook_header  = NULL;
		cJSON_ArrayForEach(webhook_header, webhook_headers)
		{
			conf_http_header *config_header =
			    NNI_ALLOC_STRUCT(config_header);
			config_header->key = nng_strdup(webhook_header->string);
			config_header->value =
			    nng_strdup(webhook_header->valuestring);
			cvector_push_back(webhook->headers, config_header);
		}
		webhook->header_count = cvector_size(webhook->headers);

		hocon_read_enum_base(webhook, encode_payload, "body.encoding",
		    jso_webhook, webhook_encoding);

		cJSON    *jso_webhook_tls = hocon_get_obj("ssl", jso_webhook);
		conf_tls *webhook_tls     = &(webhook->tls);
		conf_tls_parse_ver2_base(webhook_tls, jso_webhook_tls);
		conf_web_hook_parse_rules_ver2(config, jso);
	}

	return;
}

static void
conf_auth_parse_ver2(conf *config, cJSON *jso)
{
	conf_auth *auth         = &(config->auths);
	cJSON     *ele          = NULL;
	auth->enable            = true;
	cJSON_ArrayForEach(ele, jso)
	{
		if (cJSON_IsString(ele)) {
			cvector_push_back(
			    auth->usernames, nng_strdup(ele->string));
			cvector_push_back(
			    auth->passwords, nng_strdup(ele->valuestring));
		}
	}
	auth->count = cvector_size(auth->usernames);

	return;
}

static void
conf_auth_http_req_parse_ver2(conf_auth_http_req *config, cJSON *jso)
{
	hocon_read_str(config, url, jso);
	hocon_read_str(config, method, jso);
	if (nni_strcasecmp(config->method, "POST") != 0 &&
	    nni_strcasecmp(config->method, "GET") != 0) {
		nng_strfree(config->method);
		config->method = nng_strdup("POST");
	}
	cJSON *jso_headers = hocon_get_obj("headers", jso);
	cJSON *jso_header  = NULL;
	cJSON_ArrayForEach(jso_header, jso_headers)
	{
		conf_http_header *config_header =
		    NNI_ALLOC_STRUCT(config_header);
		config_header->key   = nng_strdup(jso_header->string);
		config_header->value = nng_strdup(jso_header->valuestring);
		cvector_push_back(config->headers, config_header);
	}
	config->header_count = cvector_size(config->headers);

	cJSON *jso_params = hocon_get_obj("params", jso);
	cJSON *jso_param  = NULL;
	cJSON_ArrayForEach(jso_param, jso_params)
	{
		conf_http_param *param = NNI_ALLOC_STRUCT(param);
		param->name            = nng_strdup(jso_param->string);
		char c                 = 0;
		if (1 == sscanf(jso_param->valuestring, "%%%c", &c)) {
			switch (c) {
			case 'A':
				param->type = ACCESS;
				break;
			case 'u':
				param->type = USERNAME;
				break;
			case 'c':
				param->type = CLIENTID;
				break;
			case 'a':
				param->type = IPADDRESS;
				break;
			case 'P':
				param->type = PASSWORD;
				break;
			case 'p':
				param->type = SOCKPORT;
				break;
			case 'C':
				param->type = COMMON_NAME;
				break;
			case 'd':
				param->type = SUBJECT;
				break;
			case 't':
				param->type = TOPIC;
				break;
			case 'm':
				param->type = MOUNTPOINT;
				break;
			case 'r':
				param->type = PROTOCOL;
				break;
			default:
				break;
			}
			cvector_push_back(config->params, param);
		} else {
			nng_strfree(param->name);
			NNI_FREE_STRUCT(param);
		}
	}
	config->param_count = cvector_size(config->params);

	cJSON *   jso_http_req_tls = hocon_get_obj("ssl", jso);
	conf_tls *http_req_tls     = &(config->tls);
	conf_tls_parse_ver2_base(http_req_tls, jso_http_req_tls);
}

static void
conf_auth_http_parse_ver2(conf *config, cJSON *jso)
{
	conf_auth_http *auth_http = &(config->auth_http);

	if (jso) {
		auth_http->enable = true;
		char *timeout =
		    cJSON_GetStringValue(hocon_get_obj("timeout", jso));
		if (timeout)
			get_time(timeout, &auth_http->timeout);

		char *connect_timeout = cJSON_GetStringValue(
		    hocon_get_obj("connect_timeout", jso));
		if (connect_timeout)
			get_time(connect_timeout, &auth_http->connect_timeout);

		hocon_read_num(auth_http, pool_size, jso);

		conf_auth_http_req *auth_http_req = &(auth_http->auth_req);
		cJSON *jso_auth_http_req = hocon_get_obj("auth_req", jso);
		conf_auth_http_req_parse_ver2(
		    auth_http_req, jso_auth_http_req);

		conf_auth_http_req *auth_http_super_req =
		    &(auth_http->super_req);
		cJSON *jso_auth_http_super_req =
		    hocon_get_obj("super_req", jso);
		conf_auth_http_req_parse_ver2(
		    auth_http_super_req, jso_auth_http_super_req);
#ifdef ACL_SUPP
		conf_auth_http_req *auth_http_acl_req = &(auth_http->acl_req);
		cJSON *jso_auth_http_acl_req = hocon_get_obj("acl_req", jso);
		conf_auth_http_req_parse_ver2(
		    auth_http_acl_req, jso_auth_http_acl_req);
#endif
	}
	return;
}

static conf_user_property **
conf_bridge_user_property_parse_ver2(cJSON *jso_prop, size_t *sz)
{
	conf_user_property **ups = NULL;
	conf_user_property * up  = NULL;

	*sz = 0;

	cJSON *jso_up = hocon_get_obj("user_property", jso_prop);

	cJSON *jso_item = NULL;
	cJSON_ArrayForEach(jso_item, jso_up)
	{
		up        = NNI_ALLOC_STRUCT(up);
		up->key   = nni_strdup(jso_item->string);
		up->value = nni_strdup(jso_item->valuestring);
		cvector_push_back(ups, up);
	}

	*sz = cvector_size(ups);
	return ups;
}

static void
conf_bridge_sub_properties_parse_ver2(conf_bridge_node *node, cJSON *jso_prop)
{
	conf_bridge_sub_properties *prop = node->sub_properties =
	    NNI_ALLOC_STRUCT(node->sub_properties);

	conf_bridge_sub_properties_init(prop);
	hocon_read_num(prop, identifier, jso_prop);

	prop->user_property = conf_bridge_user_property_parse_ver2(
	    jso_prop, &prop->user_property_size);
}

static void
conf_bridge_conn_properties_parse_ver2(conf_bridge_node *node, cJSON *jso_prop)
{
	conf_bridge_conn_properties *prop = node->conn_properties =
	    NNI_ALLOC_STRUCT(node->conn_properties);

	conf_bridge_conn_properties_init(prop);

	hocon_read_num(prop, session_expiry_interval, jso_prop);

	hocon_read_num_base(prop, request_problem_info,
	    "request_problem_infomation", jso_prop);
	hocon_read_num_base(prop, request_response_info,
	    "request_response_infomation", jso_prop);
	hocon_read_num(prop, receive_maximum, jso_prop);
	hocon_read_num(prop, topic_alias_maximum, jso_prop);
	hocon_read_num(prop, maximum_packet_size, jso_prop);

	prop->user_property = conf_bridge_user_property_parse_ver2(
	    jso_prop, &prop->user_property_size);
}

static void
conf_bridge_conn_will_properties_parse_ver2(
    conf_bridge_node *node, cJSON *jso_prop)
{
	conf_bridge_conn_will_properties *prop = node->will_properties =
	    NNI_ALLOC_STRUCT(node->will_properties);

	conf_bridge_conn_will_properties_init(prop);

	hocon_read_num(prop, payload_format_indicator, jso_prop);
	hocon_read_num(prop, message_expiry_interval, jso_prop);
	hocon_read_num(prop, will_delay_interval, jso_prop);

	hocon_read_str(prop, content_type, jso_prop);
	hocon_read_str(prop, response_topic, jso_prop);
	hocon_read_str(prop, correlation_data, jso_prop);

	prop->user_property = conf_bridge_user_property_parse_ver2(
	    jso_prop, &prop->user_property_size);
}

static void
conf_bridge_connector_parse_ver2(conf_bridge_node *node, cJSON *jso_connector)
{
	if (!jso_connector) {
		log_error("bridge connecter should not be null");
	}
	hocon_read_str_base(node, address, "server", jso_connector);
	hocon_read_num(node, proto_ver, jso_connector);
	hocon_read_str(node, clientid, jso_connector);
	hocon_read_time(node, keepalive, jso_connector);
	hocon_read_time(node, backoff_max, jso_connector);
	hocon_read_bool(node, clean_start, jso_connector);
	hocon_read_str(node, username, jso_connector);
	hocon_read_str(node, password, jso_connector);

	cJSON *   jso_tls         = hocon_get_obj("ssl", jso_connector);
	conf_tls *bridge_node_tls = &(node->tls);
	conf_tls_parse_ver2_base(bridge_node_tls, jso_tls);

	cJSON    *jso_tcp         = hocon_get_obj("tcp", jso_connector);
	conf_tcp *bridge_node_tcp = &(node->tcp);
	conf_tcp_parse_ver2_base(bridge_node_tcp, jso_tcp);

	cJSON *jso_prop = hocon_get_obj("conn_properties", jso_connector);
	if (jso_prop != NULL) {
		conf_bridge_conn_properties_parse_ver2(node, jso_prop);
	}

	cJSON *jso_will = hocon_get_obj("will", jso_connector);
	hocon_read_str_base(node, will_payload, "payload", jso_will);
	hocon_read_str_base(node, will_topic, "topic", jso_will);
	hocon_read_bool_base(node, will_retain, "retain", jso_will);
	hocon_read_num_base(node, will_qos, "qos", jso_will);

	if (node->will_payload != NULL && node->will_topic != NULL) {
		node->will_flag = true;

		jso_prop = hocon_get_obj("properties", jso_will);
		if (jso_prop != NULL) {
			conf_bridge_conn_will_properties_parse_ver2(
			    node, jso_prop);
		}
	} else {
		node->will_flag = false;
	}
}

#if defined(SUPP_QUIC)
static void
conf_bridge_quic_parse_ver2(conf_bridge_node *node, cJSON *jso_bridge_node)
{
	hocon_read_time_base(
	    node, qkeepalive, "quic_keepalive", jso_bridge_node);
	hocon_read_time_base(
	    node, qidle_timeout, "quic_idle_timeout", jso_bridge_node);
	hocon_read_time_base(
	    node, qdiscon_timeout, "quic_discon_timeout", jso_bridge_node);
	hocon_read_time_base(
	    node, qsend_idle_timeout, "quic_send_idle_timeout", jso_bridge_node);
	hocon_read_time_base(
	    node, qinitial_rtt_ms, "quic_initial_rtt_ms", jso_bridge_node);
	hocon_read_time_base(
	    node, qmax_ack_delay_ms, "quic_max_ack_delay_ms", jso_bridge_node);
	hocon_read_time_base(
	    node, qconnect_timeout, "quic_handshake_timeout", jso_bridge_node);
	hocon_read_bool_base(node, hybrid, "hybrid_bridging", jso_bridge_node);
	hocon_read_bool_base(node, quic_0rtt, "quic_0rtt", jso_bridge_node);
	hocon_read_bool_base(node, multi_stream, "quic_multi_stream", jso_bridge_node);
	hocon_read_bool_base(node, qos_first, "quic_qos_priority", jso_bridge_node);
	node->qcongestion_control = 0; // only support cubic
	// char *cc = cJSON_GetStringValue(
	//     cJSON_GetObjectItem(jso_bridge_node, "congestion_control"));
	// if (NULL != cc) {
	// 	if (0 == nng_strcasecmp(cc, "bbr")) {
	// 		node->qcongestion_control = 1;
	// 	} else if (0 == nng_strcasecmp(cc, "cubic")) {
	// 		node->qcongestion_control = 0;
	// 	} else {
	// 		node->qcongestion_control = 1;
	// 		log_error("unsupport congestion control "
	// 		         "algorithm, use "
	// 		         "default bbr!");
	// 	}
	// } else {
	// 	node->qcongestion_control = 1;
	// 	log_error("Unsupport congestion control algorithm, use "
	// 	         "default bbr!");
	// }
}
#endif

void
conf_bridge_node_parse(
    conf_bridge_node *node, conf_sqlite *bridge_sqlite, cJSON *obj)
{
	node->name = nng_strdup(obj->string);
	conf_bridge_connector_parse_ver2(node, obj);
	node->sqlite         = bridge_sqlite;
	node->enable         = true;
#if defined(SUPP_QUIC)
	conf_bridge_quic_parse_ver2(node, obj);
#endif

	cJSON *forwards = hocon_get_obj("forwards", obj);

	cJSON *forward = NULL;
	cJSON_ArrayForEach(forward, forwards)
	{
		topics *s = NNI_ALLOC_STRUCT(s);
		s->retain = NO_RETAIN;
		hocon_read_str(s, remote_topic, forward);
		hocon_read_str(s, local_topic, forward);
		cJSON *jso_key = cJSON_GetObjectItem(forward, "retain");
		if (cJSON_IsNumber(jso_key) &&
		    (jso_key->valuedouble == 0 || jso_key->valuedouble == 1)) {
			s->retain = jso_key->valuedouble;
		}
		if (!s->remote_topic || !s->local_topic) {
			log_warn("remote_topic/local_topic not found");
			if (s->remote_topic) {
				nng_strfree(s->remote_topic);
			} else if (s->local_topic) {
				nng_strfree(s->local_topic);
			}
			NNI_FREE_STRUCT(s);
			continue;
		}
		s->remote_topic_len = strlen(s->remote_topic);
		s->local_topic_len = strlen(s->local_topic);
		for (int i=0; i<(int)s->remote_topic_len; ++i)
			if (s->remote_topic[i] == '+' || s->remote_topic[i] == '#') {
				log_error("No wildcard +/# should be contained in remote topic in forward rules.");
				break;
			}
		cvector_push_back(node->forwards_list, s);
	}
	node->forwards_count = cvector_size(node->forwards_list);

	cJSON *subscriptions = hocon_get_obj("subscription", obj);
	
	cJSON *subscription = NULL;
	cJSON_ArrayForEach(subscription, subscriptions)
	{
		topics *s = NNI_ALLOC_STRUCT(s);
		s->retain = NO_RETAIN;
		hocon_read_str(s, remote_topic, subscription);
		hocon_read_str(s, local_topic, subscription);
		hocon_read_num(s, qos, subscription);
		cJSON *jso_key = cJSON_GetObjectItem(subscription, "retain");
		if (cJSON_IsNumber(jso_key) &&
		    (jso_key->valuedouble == 0 || jso_key->valuedouble == 1)) {
			s->retain = jso_key->valuedouble;
		}
		hocon_read_num(s, retain_as_published, subscription);
		hocon_read_num(s, retain_handling, subscription);
		if (!s->remote_topic || !s->local_topic) {
			log_warn("remote_topic/local_topic not found");
			if (s->remote_topic) {
				nng_strfree(s->remote_topic);
			} else if (s->local_topic) {
				nng_strfree(s->local_topic);
			}
			NNI_FREE_STRUCT(s);
			continue;
		}
		s->remote_topic_len = strlen(s->remote_topic);
		s->local_topic_len = strlen(s->local_topic);
		for (int i=0; i<(int)s->local_topic_len; ++i)
			if (s->local_topic[i] == '+' || s->local_topic[i] == '#') {
				log_error("No wildcard +/# should be contained in local topic in subscription rules");
				break;
			}
		s->stream_id = 0;
		hocon_read_num(s, stream_id, subscription);
		cvector_push_back(node->sub_list, s);
	}
	node->sub_count = cvector_size(node->sub_list);

	cJSON *jso_prop = hocon_get_obj("sub_properties", obj);
	if (jso_prop != NULL) {
		conf_bridge_sub_properties_parse_ver2(node, jso_prop);
	}

	hocon_read_num_base(node, parallel, "max_parallel_processes", obj);
	hocon_read_num(node, max_recv_queue_len, obj);
	hocon_read_num(node, max_send_queue_len, obj);
}

static void
conf_bridge_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *node_array = hocon_get_obj("bridges.mqtt", jso);
	cJSON *node_item  = NULL;

	conf_sqlite *bridge_sqlite = &(config->bridge.sqlite);
	config->bridge.nodes = NULL;
	cJSON_ArrayForEach(node_item, node_array)
	{
		if (nng_strcasecmp(node_item->string, "cache") == 0) {
			bridge_sqlite->enable      = true;
			hocon_read_num(
			    bridge_sqlite, disk_cache_size, node_item);
			hocon_read_num(
			    bridge_sqlite, flush_mem_threshold, node_item);
			hocon_read_num(
			    bridge_sqlite, resend_interval, node_item);
			hocon_read_str(
			    bridge_sqlite, mounted_file_path, node_item);

		} else {
			conf_bridge_node *node = NNI_ALLOC_STRUCT(node);
			nng_mtx_alloc(&node->mtx);
			conf_bridge_node_init(node);
			conf_bridge_node_parse(node, bridge_sqlite, node_item);
			cvector_push_back(config->bridge.nodes, node);
			config->bridge_mode |= node->enable;

		}
	}

	config->bridge.count = cvector_size(config->bridge.nodes);

	return;
}

#if defined(SUPP_PLUGIN)
static void
conf_plugin_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *libs = hocon_get_obj("plugin.libs", jso);
	cJSON *lib	      = NULL;

	config->plugin.libs     = NULL;
	config->plugin.path_sz  = 0;

	cJSON_ArrayForEach(lib, libs) {
		cJSON *jso_path = cJSON_GetObjectItem(lib, "path");
		if (jso_path) {
			if (cJSON_IsString(jso_path)) {
				if (jso_path->valuestring != NULL) {
					conf_plugin_lib *lib = NNI_ALLOC_STRUCT(lib);
					lib->path = nng_strdup(jso_path->valuestring);
					if (lib->path != NULL) {
						cvector_push_back(config->plugin.libs, lib);
					} else {
						NNI_FREE_STRUCT(lib);
						log_error("nng_strdup failed");
					}
				}
			}
		}
	}
	config->plugin.path_sz = cvector_size(config->plugin.libs);

	return;
}
#endif

void
conf_exchange_node_parse(conf_exchange_node *node, cJSON *obj)
{
	cJSON *exchange = hocon_get_obj("exchange", obj);

	hocon_read_str(node, name, exchange);
	hocon_read_str(node, topic, exchange);
	if (node->name == NULL || node->topic == NULL) {
		log_error("invalid exchange configuration!");
		return;
	}

	cJSON *rb = hocon_get_obj("ringbus", exchange);

	ringBuffer_node *rb_node = NNI_ALLOC_STRUCT(rb_node);
	if (rb_node == NULL) {
		return;
	}

	rb_node->cap = 0;
	rb_node->fullOp = 0;

	hocon_read_str(rb_node, name, rb);
	hocon_read_num(rb_node, cap, rb);
	hocon_read_num(rb_node, fullOp, rb);

	if (rb_node->name == NULL || rb_node->cap == 0) {
		log_error("exchange: ringbuffer: name/cap not found");
		NNI_FREE_STRUCT(rb_node);
		return;
	}

	cvector_push_back(node->rbufs, rb_node);
	node->rbufs_sz = cvector_size(node->rbufs);
}

static void
conf_exchange_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *node_array = hocon_get_obj("exchange_client", jso);
	cJSON *node_item  = NULL;

	cJSON_ArrayForEach(node_item, node_array)
	{
		conf_exchange_node *node = NNI_ALLOC_STRUCT(node);
		node->sock     = NULL;
		node->topic    = NULL;
		node->name     = NULL;
		node->rbufs    = NULL;
		node->rbufs_sz = 0;
		conf_exchange_node_parse(node, node_item);
		nng_mtx_alloc(&node->mtx);
		cvector_push_back(config->exchange.nodes, node);
	}
	config->exchange.count = cvector_size(config->exchange.nodes);

	return;
}

static void
conf_parquet_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_parquet = cJSON_GetObjectItem(jso, "parquet");
	if (jso_parquet) {
		conf_parquet *parquet = &(config->parquet);
		parquet->enable       = true;
		hocon_read_bool_base(parquet, enable, "enable", jso_parquet);
		hocon_read_num(parquet, file_count, jso_parquet);
		hocon_read_size(parquet, file_size, jso_parquet);
		hocon_read_str(parquet, dir, jso_parquet);
		hocon_read_str(parquet, file_name_prefix, jso_parquet);
		hocon_read_enum_base(parquet, comp_type, "compress",
		    jso_parquet, compress_type);
		cJSON *jso_parquet_encryption =
		    cJSON_GetObjectItem(jso_parquet, "encryption");
		if (jso_parquet_encryption) {
			conf_parquet_encryption *encryption =
			    &(parquet->encryption);
			encryption->enable = true;
			hocon_read_str(
			    encryption, key_id, jso_parquet_encryption);
			hocon_read_str(
			    encryption, key, jso_parquet_encryption);
			hocon_read_enum(encryption, type,
			    jso_parquet_encryption, encryption_type);
		}
	}

	return;
}

#if defined(SUPP_AWS_BRIDGE)
static void
conf_aws_bridge_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *bridge_aws_nodes = hocon_get_obj("bridges.aws", jso);
	cJSON *bridge_aws_node  = NULL;

	config->aws_bridge.count = cJSON_GetArraySize(bridge_aws_nodes);
	config->aws_bridge.nodes = NULL;
	cJSON_ArrayForEach(bridge_aws_node, bridge_aws_nodes)
	{
		conf_bridge_node *node = NNI_ALLOC_STRUCT(node);
		nng_mtx_alloc(&node->mtx);
		conf_bridge_node_init(node);
		node->name = nng_strdup(bridge_aws_node->string);
		node->enable = true;
		config->bridge_mode |= node->enable;

		conf_bridge_connector_parse_ver2(node, bridge_aws_node);

		if (node->address) {
			char *p = NULL;
			if (NULL != (p = strchr(node->address, ':'))) {
				*p = '\0';
				node->host = nng_strdup(node->address);
				node->port = atoi(++p);
				*(p-1) = ':';
			}
		}

		cJSON *forwards = hocon_get_obj("forwards", bridge_aws_node);

		cJSON *forward = NULL;
		cJSON_ArrayForEach(forward, forwards)
		{
			topics *s = NNI_ALLOC_STRUCT(s);
			hocon_read_str(s, remote_topic, forward);
			hocon_read_str(s, local_topic, forward);
			if (!s->remote_topic || !s->local_topic) {
				log_warn("remote_topic/local_topic not found");
				if (s->remote_topic) {
					nng_strfree(s->remote_topic);
				} else if (s->local_topic) {
					nng_strfree(s->local_topic);
				}
				NNI_FREE_STRUCT(s);
				continue;
			}
			s->remote_topic_len = strlen(s->remote_topic);
			s->local_topic_len = strlen(s->local_topic);

			for (int i=0; i<(int)s->remote_topic_len; ++i)
				if (s->remote_topic[i] == '+' || s->remote_topic[i] == '#') {
					log_error("No wildcard +/# should be contained in remote topic in forward rules.");
					break;
				}
			cvector_push_back(node->forwards_list, s);
		}
		node->forwards_count = cvector_size(node->forwards_list);

		cJSON *subscriptions =
		    hocon_get_obj("subscription", bridge_aws_node);

		cJSON *subscription = NULL;
		cJSON_ArrayForEach(subscription, subscriptions)
		{
			topics *s = NNI_ALLOC_STRUCT(s);
			hocon_read_str(s, remote_topic, subscription);
			hocon_read_str(s, local_topic, subscription);
			hocon_read_num(s, qos, subscription);
			if (!s->remote_topic || !s->local_topic) {
				log_warn("remote_topic/local_topic not found");
				if (s->remote_topic) {
					nng_strfree(s->remote_topic);
				} else if (s->local_topic) {
					nng_strfree(s->local_topic);
				}
				NNI_FREE_STRUCT(s);
				continue;
			}
			s->remote_topic_len = strlen(s->remote_topic);
			s->local_topic_len = strlen(s->local_topic);
			s->stream_id = 0;
			hocon_read_num(s, stream_id, subscription);

			for (int i=0; i<(int)s->local_topic_len; ++i)
				if (s->local_topic[i] == '+' || s->local_topic[i] == '#') {
					log_error("No wildcard +/# should be contained in local topic in subscription rules");
					break;
				}
			cvector_push_back(node->sub_list, s);
		}

		node->sub_count = cvector_size(node->sub_list);

		hocon_read_num_base(
		    node, parallel, "max_parallel_processes", bridge_aws_node);
		cvector_push_back(config->aws_bridge.nodes, node);
	}

	return;
}
#endif

#if defined(SUPP_RULE_ENGINE)
static void
conf_rule_parse_ver2(conf *config, cJSON *jso)
{

	conf_rule *cr              = &(config->rule_eng);
	cJSON *    jso_rule_sqlite = hocon_get_obj("rules.sqlite", jso);
	cJSON *jso_rules = NULL;
	cJSON *jso_rule = NULL;

	nng_mtx_alloc(&(cr->rule_mutex));

	if (jso_rule_sqlite) {
#ifndef NNG_SUPP_SQLITE
		log_error("If you want use sqlite rule, recompile nanomq with option `-DNNG_ENABLE_SQLITE=ON`");
	}
#else
		hocon_read_str_base(cr, sqlite_db, "path", jso_rule_sqlite);
		jso_rules = hocon_get_obj("rules", jso_rule_sqlite);
		cr->option |= RULE_ENG_SDB;
		jso_rule  = NULL;

		cJSON_ArrayForEach(jso_rule, jso_rules)
		{

			rule r = { 0 };
			r.enabled = true;
			hocon_read_str_base(&r, raw_sql, "sql", jso_rule);
			hocon_read_str_base(&r, sqlite_table, "table", jso_rule);

			rule_sql_parse(cr, r.raw_sql);

			cr->rules[cvector_size(cr->rules) - 1].sqlite_table =
			    r.sqlite_table;
			cr->rules[cvector_size(cr->rules) - 1].forword_type =
			    RULE_FORWORD_SQLITE;
			cr->rules[cvector_size(cr->rules) - 1].raw_sql = r.raw_sql;
			cr->rules[cvector_size(cr->rules) - 1].enabled = r.enabled;
			cr->rules[cvector_size(cr->rules) - 1].rule_id =
			    rule_generate_rule_id();
		}
	}
#endif

	cJSON *jso_rule_repub = hocon_get_obj("rules.repub", jso);

	if (jso_rule_repub) {

		cr->option |= RULE_ENG_RPB;

		jso_rules = hocon_get_obj("rules", jso_rule_repub);
		jso_rule  = NULL;

		cJSON_ArrayForEach(jso_rule, jso_rules)
		{
			rule re        = { 0 };
			re.enabled     = true;
			repub_t *repub = NNI_ALLOC_STRUCT(repub);
			hocon_read_str_base(&re, raw_sql, "sql", jso_rule);
			hocon_read_str_base(
			    repub, address, "server", jso_rule);
			hocon_read_str(repub, topic, jso_rule);
			hocon_read_num(repub, proto_ver, jso_rule);
			hocon_read_str(repub, clientid, jso_rule);
			hocon_read_num(repub, keepalive, jso_rule);
			hocon_read_bool(repub, clean_start, jso_rule);
			hocon_read_str(repub, username, jso_rule);
			hocon_read_str(repub, password, jso_rule);

			rule_sql_parse(cr, re.raw_sql);

			cr->rules[cvector_size(cr->rules) - 1].repub = repub;
			cr->rules[cvector_size(cr->rules) - 1].forword_type =
			    RULE_FORWORD_REPUB;
			cr->rules[cvector_size(cr->rules) - 1].raw_sql =
			    re.raw_sql;
			cr->rules[cvector_size(cr->rules) - 1].enabled =
			    re.enabled;
			cr->rules[cvector_size(cr->rules) - 1].rule_id =
			    rule_generate_rule_id();
		}
	}

	cJSON *jso_rule_mysql = hocon_get_obj("rules.mysql", jso);
	if (jso_rule_mysql) {
#ifndef SUPP_MYSQL
		log_error("If you want use mysql rule, recompile nanomq with option `-DENABLE_MYSQL=ON`");
	}
#else
		cr->option |= RULE_ENG_MDB;

		// TODO support multiple mysql database
		cJSON *ele = NULL;
		cJSON_ArrayForEach(ele, jso_rule_mysql)
		{
			cJSON *jso_conn  = cJSON_GetObjectItem(ele, "conn");
			cJSON *jso_rules = cJSON_GetObjectItem(ele, "rules");
			rule_mysql sql   = { 0 };

			if (jso_conn) {
				hocon_read_str_base(
				    cr, mysql_db, "database", jso_conn);

				hocon_read_str(&sql, host, jso_conn);
				hocon_read_str(&sql, username, jso_conn);
				hocon_read_str(&sql, password, jso_conn);
			}

			jso_rule = NULL;

			cJSON_ArrayForEach(jso_rule, jso_rules)
			{
				rule r    = { 0 };
				r.enabled = true;
				hocon_read_str_base(
				    &r, raw_sql, "sql", jso_rule);
				rule_mysql *mysql = NNI_ALLOC_STRUCT(mysql);

				rule_sql_parse(cr, r.raw_sql);

				mysql->host     = nng_strdup(sql.host);
				mysql->username = nng_strdup(sql.username);
				mysql->password = nng_strdup(sql.password);
				hocon_read_str(mysql, table, jso_rule);

				cr->rules[cvector_size(cr->rules) - 1].mysql =
				    mysql;
				cr->rules[cvector_size(cr->rules) - 1]
				    .forword_type = RULE_FORWORD_MYSQL;
				cr->rules[cvector_size(cr->rules) - 1]
				    .raw_sql = r.raw_sql;
				cr->rules[cvector_size(cr->rules) - 1]
				    .enabled = r.enabled;
				cr->rules[cvector_size(cr->rules) - 1]
				    .rule_id = rule_generate_rule_id();
			}

			nng_strfree(sql.host);
			nng_strfree(sql.username);
			nng_strfree(sql.password);
		}
	}
#endif

	return;
}
#endif

char *
json_buffer_from_fp(FILE *fp)
{

	char *buffer    = NULL;
	char  buf[8192] = { 0 };

	while (!feof(fp)) {
		fread(buf, sizeof(buf), 1, fp);
		for (size_t i = 0; i < sizeof(buf); i++) {
			cvector_push_back(buffer, buf[i]);
		}
		memset(buf, 0, 8192);
	}

	return buffer;
}

static void
conf_acl_parse_ver2(conf *config, cJSON *jso)
{
#ifdef ACL_SUPP
	conf_acl *acl = &config->acl;
	acl->enable = true;

	cJSON *rule_list = hocon_get_obj("rules", jso);
	if (cJSON_IsArray(rule_list)) {
		size_t count = (size_t) cJSON_GetArraySize(rule_list);
		for (size_t i = 0; i < count; i++) {
			cJSON *rule_item = cJSON_GetArrayItem(rule_list, i);
			acl_rule *rule;
			if (acl_parse_json_rule(rule_item, i, &rule)) {
				acl->rule_count++;
				cvector_push_back(acl->rules, rule);
			}
		}
	}
#else
	NNI_ARG_UNUSED(config);
	NNI_ARG_UNUSED(jso);
#endif
}

static void
conf_authorization_prase_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_auth_http = hocon_get_obj("auth.http_auth", jso);
	if (jso_auth_http) {
		conf_auth_http_parse_ver2(config, jso_auth_http);
	}
	// TODO if not use include, we should read file manually.
	cJSON *jso_auth_pwd = hocon_get_obj("auth.password", jso);
	if (jso_auth_pwd) {
		conf_auth_parse_ver2(config, jso_auth_pwd);
	}
	cJSON *jso_auth_acl = hocon_get_obj("auth.acl", jso);
	if (jso_auth_acl) {
		conf_acl_parse_ver2(config, jso_auth_acl);
	}
}

void
conf_parse_ver2(conf *config)
{
	log_add_console(NNG_LOG_INFO, NULL);
	const char *conf_path = config->conf_file;
	if (conf_path == NULL || !nano_file_exists(conf_path)) {
		if (!nano_file_exists(CONF_PATH_NAME)) {
			log_error("Configure file [%s] or [%s] not found or "
			          "unreadable",
			    conf_path, CONF_PATH_NAME);
			log_clear_callback();
			return;
		} else {
			conf_path = CONF_PATH_NAME;
		}
	}

	cJSON *jso = hocon_parse_file(conf_path);
	if (NULL != jso) {
		conf_basic_parse_ver2(config, jso);
		conf_set_threads(config);
		conf_sqlite_parse_ver2(config, jso);
		conf_tls_parse_ver2(config, jso);
#if defined(ENABLE_LOG)
		conf_log_parse_ver2(config, jso);
#endif
		conf_webhook_parse_ver2(config, jso);
		conf_authorization_prase_ver2(config, jso);
		conf_bridge_parse_ver2(config, jso);
		conf_exchange_parse_ver2(config, jso);
		conf_parquet_parse_ver2(config, jso);
#if defined(SUPP_PLUGIN)
		conf_plugin_parse_ver2(config, jso);
#endif
#if defined(SUPP_AWS_BRIDGE)
		conf_aws_bridge_parse_ver2(config, jso);
#endif

#if defined(SUPP_RULE_ENGINE)
		conf_rule_parse_ver2(config, jso);
#endif

		cJSON_Delete(jso);
	}

	log_clear_callback();

	return;
}

#if defined(SUPP_ZMQ_GATEWAY)
void printf_zmq_gateway_conf(zmq_gateway_conf *config)
{
	printf("zmq gateway conf zmq_sub_url:            %s\n",
	    config->zmq_sub_url);
	printf("zmq gateway conf zmq_pub_url:            %s\n",
	    config->zmq_pub_url);
	printf(
	    "zmq gateway conf mqtt_url:               %s\n", config->mqtt_url);
	printf("zmq gateway conf sub_topic:              %s\n",
	    config->sub_topic);
	printf("zmq gateway conf pub_topic:              %s\n",
	    config->pub_topic);
	printf("zmq gateway conf zmq_sub_pre:            %s\n",
	    config->zmq_sub_pre);
	printf("zmq gateway conf zmq_pub_pre:            %s\n",
	    config->zmq_pub_pre);
	printf("zmq gateway conf path:                   %s\n", config->path);
	printf(
	    "zmq gateway conf username:               %s\n", config->username);
	printf(
	    "zmq gateway conf password:               %s\n", config->password);
	printf("zmq gateway conf proto_ver:    	         %d\n",
	    config->proto_ver);
	printf("zmq gateway conf keepalive:    	         %d\n",
	    config->keepalive);
	printf("zmq gateway conf clean_start:  	         %d\n",
	    config->clean_start);
	printf("zmq gateway conf parallel:     	         %d\n",
	    config->parallel);
	printf("zmq gateway conf zmq type:     	         %s\n",
	    config->type == PUB_SUB ? "pub_sub" : "req_rep");
}

void
conf_gateway_parse_ver2(zmq_gateway_conf *config)
{
	const char *dest_path = config->path;

	if (dest_path == NULL || !nano_file_exists(dest_path)) {
		if (!nano_file_exists(CONF_ZMQ_GATEWAY_PATH_NAME)) {
			log_debug("Configure file [%s] or [%s] not found or "
			          "unreadable\n",
			    dest_path, CONF_ZMQ_GATEWAY_PATH_NAME);
			return;
		} else {
			dest_path = CONF_ZMQ_GATEWAY_PATH_NAME;
		}
	}

	cJSON *jso      = hocon_parse_file(dest_path);
	cJSON *jso_mqtt = hocon_get_obj("gateway.mqtt", jso);
	cJSON *jso_zmq  = hocon_get_obj("gateway.zmq", jso);

	hocon_read_num(config, proto_ver, jso_mqtt);
	hocon_read_num(config, keepalive, jso_mqtt);
	hocon_read_bool(config, clean_start, jso_mqtt);
	hocon_read_num(config, parallel, jso_mqtt);
	hocon_read_str_base(config, mqtt_url, "address", jso_mqtt);
	hocon_read_str_base(config, pub_topic, "forward", jso_mqtt);
	hocon_read_str(config, sub_topic, jso_mqtt);

	hocon_read_str_base(config, zmq_sub_pre, "sub_pre", jso_zmq);
	hocon_read_str_base(config, zmq_pub_pre, "pub_pre", jso_zmq);
	hocon_read_str_base(config, zmq_sub_url, "sub_address", jso_zmq);
	hocon_read_str_base(config, zmq_pub_url, "pub_address", jso_zmq);

	conf_http_server_init(&(config->http_server), 8082);
	conf_http_server_parse_ver2(&(config->http_server), jso);

	cJSON_Delete(jso);

	printf_zmq_gateway_conf(config);

	return;
}
#endif

#if defined(SUPP_VSOMEIP_GATEWAY)
void
conf_vsomeip_gateway_parse_ver2(vsomeip_gateway_conf *config)
{
	const char *dest_path = config->path;

	if (dest_path == NULL || !nano_file_exists(dest_path)) {
		if (!nano_file_exists(CONF_VSOMEIP_GATEWAY_PATH_NAME)) {
			log_debug("Configure file [%s] or [%s] not found or "
			          "unreadable\n",
			    dest_path, CONF_VSOMEIP_GATEWAY_PATH_NAME);
			return;
		} else {
			dest_path = CONF_VSOMEIP_GATEWAY_PATH_NAME;
		}
	}

	cJSON *jso         = hocon_parse_file(dest_path);
	cJSON *jso_mqtt    = hocon_get_obj("gateway.mqtt", jso);
	cJSON *jso_vsomeip = hocon_get_obj("gateway.vsomeip", jso);

	// Parse mqtt
	hocon_read_str_base(config, mqtt_url, "address", jso_mqtt);
	hocon_read_str(config, sub_topic, jso_mqtt);
	hocon_read_num_base(config, sub_qos, "subscription_qos", jso_mqtt);
	hocon_read_num(config, proto_ver, jso_mqtt);
	hocon_read_num(config, keepalive, jso_mqtt);
	hocon_read_bool(config, clean_start, jso_mqtt);
	hocon_read_str(config, username, jso_mqtt);
	hocon_read_str(config, password, jso_mqtt);
	hocon_read_str(config, clientid, jso_mqtt);
	hocon_read_str_base(config, pub_topic, "forward", jso_mqtt);
	hocon_read_num(config, parallel, jso_mqtt);

	// Parse vsomeip
	hocon_read_hex_str(config, service_id, jso_vsomeip);
	hocon_read_hex_str(config, service_instance_id, jso_vsomeip);
	hocon_read_hex_str(config, service_method_id, jso_vsomeip);
	hocon_read_hex_str(config, service_event_id, jso_vsomeip);
	hocon_read_hex_str(config, service_eventgroup_id, jso_vsomeip);
	hocon_read_str(config, conf_path, jso_vsomeip);

	// Parse http server 
	conf_http_server_init(&config->http_server, 8082);
	conf_http_server_parse_ver2(&config->http_server, jso);

	cJSON_Delete(jso);

	return;
}
#endif

#if defined(SUPP_DDS_PROXY)
static void
conf_dds_gateway_forward_parse_ver2(dds_gateway_forward *forward, cJSON *json)
{
	cJSON *rules = hocon_get_obj("forward_rules", json);
	cJSON *jso_item = NULL;

	if (rules == NULL) {
		return;
	}

	dds_gateway_topic **dds2mqtt = NULL;
	dds_gateway_topic **mqtt2dds = NULL;

	dds_gateway_topic *ddstopic;

	cJSON *jso_dds2mqtt = hocon_get_obj("dds_to_mqtt", rules);
	cJSON_ArrayForEach(jso_item, jso_dds2mqtt)
	{
		ddstopic = NNI_ALLOC_STRUCT(ddstopic);
		hocon_read_str_base(ddstopic, from, "from_dds", jso_item);
		hocon_read_str_base(ddstopic, to, "to_mqtt", jso_item);
		hocon_read_str(ddstopic, struct_name, jso_item);
		cvector_push_back(dds2mqtt, ddstopic);
	}
	forward->dds2mqtt = dds2mqtt;
	forward->dds2mqtt_sz = cvector_size(dds2mqtt);

	cJSON *jso_mqtt2dds = hocon_get_obj("mqtt_to_dds", rules);
	cJSON_ArrayForEach(jso_item, jso_mqtt2dds)
	{
		ddstopic = NNI_ALLOC_STRUCT(ddstopic);
		hocon_read_str_base(ddstopic, from, "from_mqtt", jso_item);
		hocon_read_str_base(ddstopic, to, "to_dds", jso_item);
		hocon_read_str(ddstopic, struct_name, jso_item);
		cvector_push_back(mqtt2dds, ddstopic);
	}
	forward->mqtt2dds = mqtt2dds;
	forward->mqtt2dds_sz = cvector_size(mqtt2dds);
}

static void
conf_dds_gateway_mqtt_parse_ver2(dds_gateway_mqtt *mqtt, cJSON *jso)
{
	cJSON *jso_mqtt       = hocon_get_obj("mqtt", jso);
	cJSON *json_mqtt_conn = hocon_get_obj("connector", jso_mqtt);

	hocon_read_str_base(mqtt, address, "server", json_mqtt_conn);
	hocon_read_num(mqtt, proto_ver, json_mqtt_conn);
	hocon_read_str(mqtt, clientid, json_mqtt_conn);
	hocon_read_time(mqtt, keepalive, json_mqtt_conn);
	hocon_read_bool(mqtt, clean_start, json_mqtt_conn);
	hocon_read_str(mqtt, username, json_mqtt_conn);
	hocon_read_str(mqtt, password, json_mqtt_conn);

	cJSON *json_conn_tls = hocon_get_obj("ssl", json_mqtt_conn);
	conf_tls_parse_ver2_base(&mqtt->tls, json_conn_tls);
}

static void
conf_dds_gateway_dds_parse_ver2(dds_gateway_dds *dds, cJSON *jso)
{
	cJSON *jso_dds = hocon_get_obj("dds", jso);

	hocon_read_str(dds, idl_type, jso_dds);
	hocon_read_num(dds, domain_id, jso_dds);
	hocon_read_str(dds, subscriber_partition, jso_dds);
	hocon_read_str(dds, publisher_partition, jso_dds);

	cJSON *jso_shm = hocon_get_obj("shared_memory", jso_dds);
	hocon_read_bool_base(dds, shm_mode, "enable", jso_shm);
	hocon_read_str_base(dds, shm_log_level, "log_level", jso_shm);
}

void
conf_dds_gateway_init(dds_gateway_conf *config)
{
	config->path = NULL;

	dds_gateway_mqtt *   mqtt    = &config->mqtt;
	dds_gateway_dds *    dds     = &config->dds;
	dds_gateway_forward *forward = &config->forward;
	conf_http_server *   http    = &config->http_server;

	conf_http_server_init(http, 8082);

	mqtt->sock        = NULL;
	mqtt->address     = NULL;
	mqtt->clean_start = true;
	mqtt->keepalive   = 60;
	mqtt->proto_ver   = 4;
	mqtt->clientid    = NULL;
	mqtt->username    = NULL;
	mqtt->password    = NULL;
	mqtt->port        = 1883;

	conf_tls_init(&mqtt->tls);

	dds->idl_type      = NULL;
	dds->domain_id     = 0;
	dds->shm_mode      = false;
	dds->shm_log_level = NULL;

	dds->subscriber_partition = NULL;
	dds->publisher_partition = NULL;

	forward->dds2mqtt_sz          = 0;
	forward->dds2mqtt             = NULL;
	forward->mqtt2dds_sz          = 0;
	forward->mqtt2dds             = NULL;
}

void
conf_dds_gateway_parse_ver2(dds_gateway_conf *config)
{
	const char *dest_path = config->path;

	if (dest_path == NULL || !nano_file_exists(dest_path)) {
		if (!nano_file_exists(CONF_DDS_GATEWAY_PATH_NAME)) {
			log_debug("Configure file [%s] or [%s] not found or "
			          "unreadable\n",
			    dest_path, CONF_DDS_GATEWAY_PATH_NAME);
			return;
		} else {
			dest_path    = CONF_DDS_GATEWAY_PATH_NAME;
			config->path = nni_strdup(dest_path);
		}
	}

	dds_gateway_mqtt *   mqtt    = &config->mqtt;
	dds_gateway_dds *    dds     = &config->dds;
	dds_gateway_forward *forward = &config->forward;
	conf_http_server *   http    = &config->http_server;

	cJSON *jso = hocon_parse_file(dest_path);

	conf_dds_gateway_mqtt_parse_ver2(mqtt, jso);
	conf_dds_gateway_dds_parse_ver2(dds, jso);
	conf_dds_gateway_forward_parse_ver2(forward, jso);
	conf_http_server_parse_ver2(http, jso);

	cJSON_Delete(jso);

	return;
}

void
conf_dds_gateway_destory(dds_gateway_conf *config)
{
	dds_gateway_mqtt *   mqtt    = &config->mqtt;
	dds_gateway_dds *    dds     = &config->dds;
	dds_gateway_forward *forward = &config->forward;
	conf_http_server *   http    = &config->http_server;

	conf_http_server_destroy(http);

	nng_strfree(config->path);

	if (mqtt->clientid) {
		free(mqtt->clientid);
	}
	if (mqtt->address) {
		free(mqtt->address);
	}
	if (mqtt->username) {
		free(mqtt->username);
	}
	if (mqtt->password) {
		free(mqtt->password);
	}
	conf_tls_destroy(&mqtt->tls);

	if (dds->idl_type) {
		free(dds->idl_type);
	}
	if (dds->subscriber_partition) {
		free(dds->subscriber_partition);
	}
	if (dds->publisher_partition) {
		free(dds->publisher_partition);
	}
	if (dds->shm_log_level) {
		free(dds->shm_log_level);
	}

	for (size_t i=0; i<forward->dds2mqtt_sz; ++i) {
		dds_gateway_topic *dds2mqtt_tp = forward->dds2mqtt[i];

		if (dds2mqtt_tp->from) {
			free(dds2mqtt_tp->from);
		}
		if (dds2mqtt_tp->to) {
			free(dds2mqtt_tp->to);
		}

		nng_strfree(dds2mqtt_tp->struct_name);
	}

	for (size_t i=0; i<forward->mqtt2dds_sz; ++i) {
		dds_gateway_topic *mqtt2dds_tp = forward->mqtt2dds[i];

		if (mqtt2dds_tp->from) {
			free(mqtt2dds_tp->from);
		}
		if (mqtt2dds_tp->to) {
			free(mqtt2dds_tp->to);
		}

		nng_strfree(mqtt2dds_tp->struct_name);
	}
}
#endif
