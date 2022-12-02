#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/acl_conf.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/hocon.h"
#include "nng/supplemental/nanolib/log.h"
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

cJSON *hocon_get_obj(char *key, cJSON *jso);

// Read json value into struct
// use same struct fields and json keys
#define hocon_read_str_base(structure, field, key, jso)               \
	do {                                                          \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);       \
		if (NULL == jso_key) {                                \
			log_error("Read config %s failed!", key);     \
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
	size_t url_len = strlen(head) + strlen(address);
	char * url     = nng_alloc(url_len + 1);
	snprintf(url, url_len, "%s%s", head, address);
	return url;
}

#define hocon_read_address_base(structure, field, key, head, jso)            \
	do {                                                                 \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);              \
		if (NULL == jso_key) {                                       \
			log_error("Read config %s failed!", key);            \
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
			log_error("Read config %s failed!", key);         \
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

#define hocon_read_bool_base(structure, field, key, jso)            \
	do {                                                        \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);     \
		if (NULL == jso_key) {                              \
			log_error("Read config %s failed!", key);   \
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
			log_error("Read config %s failed!", key);       \
			break;                                          \
		}                                                       \
		if (cJSON_IsNumber(jso_key)) {                          \
			if (jso_key->valueint > 0)                      \
				(structure)->field = jso_key->valueint; \
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
			log_error("Read config %s failed!", key);            \
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
conf_basic_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_sys = cJSON_GetObjectItem(jso, "system");
	if (NULL == jso_sys) {
		log_error("Read config system failed!");
		return;
	}
	hocon_read_bool(config, daemon, jso_sys);
	hocon_read_num(config, num_taskq_thread, jso_sys);
	hocon_read_num(config, max_taskq_thread, jso_sys);
	hocon_read_num(config, parallel, jso_sys);

	cJSON *jso_auth = cJSON_GetObjectItem(jso, "authorization");
#ifdef ACL_SUPP
	hocon_read_enum_base(
	    config, acl_nomatch, "no_match", jso_auth, auth_acl_permit);
	hocon_read_enum_base(config, acl_deny_action, "deny_action", jso_auth,
	    auth_deny_action);

	cJSON *jso_auth_cache = hocon_get_obj("authorization.cache", jso_auth);
	hocon_read_bool_base(
	    config, enable_acl_cache, "enable", jso_auth_cache);
	hocon_read_num_base(
	    config, acl_cache_max_size, "max_size", jso_auth_cache);
	hocon_read_num_base(config, acl_cache_ttl, "ttl", jso_auth_cache);
#endif
	cJSON *jso_mqtt_session = hocon_get_obj("mqtt.session", jso);
	if (NULL == jso_mqtt_session) {
		log_error("Read config listeners failed!");
		return;
	}
	hocon_read_num(config, property_size, jso_mqtt_session);
	hocon_read_num(config, max_packet_size, jso_mqtt_session);
	hocon_read_num(config, client_max_packet_size, jso_mqtt_session);
	hocon_read_num(config, msq_len, jso_mqtt_session);
	hocon_read_time(config, qos_duration, jso_mqtt_session);
	hocon_read_num_base(
	    config, backoff, "keepalive_backoff", jso_mqtt_session);
	hocon_read_bool(config, allow_anonymous, jso_mqtt_session);


	cJSON *jso_listeners = cJSON_GetObjectItem(jso, "listeners");
	if (NULL == jso_listeners) {
		log_error("Read config listeners failed!");
		return;
	}
	cJSON *jso_tcp = cJSON_GetObjectItem(jso_listeners, "tcp");
	if (NULL == jso_tcp) {
		log_error("Read config tcp failed!");
		return;
	}
	hocon_read_address_base(config, url, "bind", "nmq-tcp://", jso_tcp);
	hocon_read_bool(config, enable, jso_tcp);

	cJSON *jso_websocket = hocon_get_obj("listeners.ws", jso);
	if (NULL == jso_websocket) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}

	conf_websocket *websocket = &(config->websocket);
	hocon_read_bool(websocket, enable, jso_websocket);
	hocon_read_address_base(
	    websocket, url, "bind", "nmq-ws://", jso_websocket);

	// http server
	cJSON *jso_http_server = cJSON_GetObjectItem(jso, "http_server");
	if (NULL == jso_http_server) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}

	conf_http_server *http_server = &(config->http_server);
	hocon_read_bool(http_server, enable, jso_http_server);
	hocon_read_num(http_server, port, jso_http_server);
	hocon_read_num(http_server, parallel, jso_http_server);
	hocon_read_str(http_server, username, jso_http_server);
	hocon_read_str(http_server, password, jso_http_server);
	hocon_read_enum(
	    http_server, auth_type, jso_http_server, http_server_auth_type);
	conf_jwt *jwt = &(http_server->jwt);

	cJSON *jso_pub_key_file = hocon_get_obj("jwt.public", jso_http_server);
	cJSON *jso_pri_key_file =
	    hocon_get_obj("jwt.private", jso_http_server);
	hocon_read_str_base(jwt, public_keyfile, "keyfile", jso_pub_key_file);
	hocon_read_str_base(jwt, private_keyfile, "keyfile", jso_pri_key_file);
	if (file_load_data(jwt->public_keyfile, (void **) &jwt->public_key) >
	    0) {
		jwt->iss =
		    (char *) nni_plat_file_basename(jwt->public_keyfile);
		jwt->public_key_len = strlen(jwt->public_key);
	}

	if (file_load_data(jwt->private_keyfile, (void **) &jwt->private_key) >
	    0) {
		jwt->private_key_len = strlen(jwt->private_key);
	}

	return;
}

static void
conf_tls_parse_ver2_base(conf_tls *tls, cJSON *jso_tls)
{
	hocon_read_bool(tls, enable, jso_tls);
	hocon_read_str(tls, key_password, jso_tls);
	hocon_read_str(tls, keyfile, jso_tls);
	hocon_read_str(tls, certfile, jso_tls);
	hocon_read_str_base(tls, cafile, "cacertfile", jso_tls);
	hocon_read_bool(tls, verify_peer, jso_tls);
	hocon_read_bool_base(tls, set_fail, "fail_if_no_peer_cert", jso_tls);

	if (0 == file_load_data(tls->keyfile, (void **) &tls->key)) {
		log_error("Read %s failed!", tls->keyfile);
	}
	if (0 == file_load_data(tls->certfile, (void **) &tls->cert)) {
		log_error("Read %s failed!", tls->certfile);
	}
	if (0 == file_load_data(tls->cafile, (void **) &tls->ca)) {
		log_error("Read %s failed!", tls->cafile);
	}

	return;
}
static void
conf_tls_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_tls = hocon_get_obj("listeners.ssl", jso);
	if (NULL == jso_tls) {
		log_error("Read config tls failed!");
		return;
	}

	conf_tls *tls = &(config->tls);
	conf_tls_parse_ver2_base(tls, jso_tls);
	hocon_read_address_base(tls, url, "bind", "tls+nmq-tcp://", jso_tls);
	return;
}

static void
conf_sqlite_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_sqlite = cJSON_GetObjectItem(jso, "sqlite");
	if (NULL == jso_sqlite) {
		log_error("Read config sqlite failed!");
		return;
	}

	conf_sqlite *sqlite = &(config->sqlite);
	hocon_read_bool(sqlite, enable, jso_sqlite);
	hocon_read_num(sqlite, disk_cache_size, jso_sqlite);
	hocon_read_str(sqlite, mounted_file_path, jso_sqlite);
	hocon_read_num(sqlite, flush_mem_threshold, jso_sqlite);
	hocon_read_num(sqlite, resend_interval, jso_sqlite);

	return;
}

#if defined(ENABLE_LOG)
static void
conf_log_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_log = cJSON_GetObjectItem(jso, "log");
	if (NULL == jso_log) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}
	conf_log *log            = &(config->log);
	cJSON *   jso_log_to     = hocon_get_obj("to", jso_log);
	cJSON *   jso_log_to_ele = NULL;

	cJSON_ArrayForEach(jso_log_to_ele, jso_log_to)
	{
		if (!strcmp("file", cJSON_GetStringValue(jso_log_to_ele))) {
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
	hocon_read_str_base(log, rotation_sz_str, "size", jso_log_rotation);
	size_t num      = 0;
	char   unit[10] = { 0 };
	int    res      = sscanf(log->rotation_sz_str, "%zu%s", &num, unit);
	if (res == 2) {
		if (nni_strcasecmp(unit, "KB") == 0) {
			log->rotation_sz = num * 1024;
		} else if (nni_strcasecmp(unit, "MB") == 0) {
			log->rotation_sz = num * 1024 * 1024;
		} else if (nni_strcasecmp(unit, "GB") == 0) {
			log->rotation_sz = num * 1024 * 1024 * 1024;
		}
	}

	hocon_read_num_base(log, rotation_count, "count", jso_log_rotation);

	return;
}
#endif

static void
webhook_action_parse_ver2(cJSON *object, conf_web_hook_rule *hook_rule)
{
	cJSON *action = cJSON_GetObjectItem(object, "action");
	if (cJSON_IsString(action)) {
		const char *act_val = cJSON_GetStringValue(action);
		hook_rule->action   = nng_strdup(act_val);
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
	cJSON *jso_webhook_rules         = hocon_get_obj("webhook.rule", jso);
	cJSON *jso_webhook_rule          = NULL;
	cJSON *jso_webhook_rule_elem_arr = NULL;
	cJSON *jso_webhook_rule_elem     = NULL;

	conf_web_hook *webhook = &(config->web_hook);
	webhook->rules         = NULL;

	cJSON_ArrayForEach(jso_webhook_rule, jso_webhook_rules)
	{
		cJSON_ArrayForEach(jso_webhook_rule_elem_arr, jso_webhook_rule)
		{
			cJSON_ArrayForEach(
			    jso_webhook_rule_elem, jso_webhook_rule_elem_arr)
			{
				conf_web_hook_rule *hook_rule =
				    NNI_ALLOC_STRUCT(hook_rule);
				webhook_action_parse_ver2(
				    jso_webhook_rule_elem, hook_rule);
				hook_rule->event =
				    get_webhook_event(jso_webhook_rule->string,
				        jso_webhook_rule_elem_arr->string);
				cvector_push_back(webhook->rules, hook_rule);
			}
		}
	}
	webhook->rule_count = cvector_size(webhook->rules);

	return;
}

static void
conf_webhook_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_webhook = cJSON_GetObjectItem(jso, "webhook");
	if (NULL == jso_webhook) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}

	conf_web_hook *webhook = &(config->web_hook);
	hocon_read_bool(webhook, enable, jso_webhook);
	hocon_read_str(webhook, url, jso_webhook);
	cJSON *webhook_headers = hocon_get_obj("headers", jso_webhook);
	cJSON *webhook_header  = NULL;
	cJSON_ArrayForEach(webhook_header, webhook_headers)
	{
		conf_http_header *web_hook_http_header =
		    NNI_ALLOC_STRUCT(web_hook_http_header);
		web_hook_http_header->key = nng_strdup(webhook_header->string);
		web_hook_http_header->value =
		    nng_strdup(webhook_header->valuestring);
		cvector_push_back(webhook->headers, web_hook_http_header);
	}
	webhook->header_count = cvector_size(webhook->headers);

	hocon_read_enum_base(webhook, encode_payload, "body.encoding",
	    jso_webhook, webhook_encoding);

	cJSON *   jso_webhook_tls = hocon_get_obj("tls", jso_webhook);
	conf_tls *webhook_tls     = &(webhook->tls);
	hocon_read_bool(webhook_tls, enable, jso_webhook_tls);
	hocon_read_str(webhook_tls, key_password, jso_webhook_tls);
	hocon_read_str(webhook_tls, keyfile, jso_webhook_tls);
	hocon_read_str(webhook_tls, keyfile, jso_webhook_tls);
	hocon_read_str(webhook_tls, certfile, jso_webhook_tls);
	hocon_read_str_base(
	    webhook_tls, cafile, "cacertfile", jso_webhook_tls);

	conf_web_hook_parse_rules_ver2(config, jso);

	return;
}

static void
conf_auth_parse_ver2(conf *config, cJSON *jso)
{
	conf_auth *auth         = &(config->auths);
	cJSON *    jso_auth_ele = NULL;

	hocon_read_bool(auth, enable, jso);
	cJSON *user_arr = hocon_get_obj("users", jso);

	cJSON_ArrayForEach(jso_auth_ele, user_arr)
	{
		char *auth_username = cJSON_GetStringValue(
		    cJSON_GetObjectItem(jso_auth_ele, "username"));
		cvector_push_back(auth->usernames, nng_strdup(auth_username));
		char *auth_password = cJSON_GetStringValue(
		    cJSON_GetObjectItem(jso_auth_ele, "password"));
		cvector_push_back(auth->passwords, nng_strdup(auth_password));
	}
	auth->count = cvector_size(auth->usernames);

	return;
}

static void
conf_auth_http_req_parse_ver2(conf_auth_http_req *config, cJSON *jso)
{
	hocon_read_str(config, url, jso);
	hocon_read_str(config, method, jso);
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

	cJSON *   jso_http_req_tls = hocon_get_obj("tls", jso);
	conf_tls *http_req_tls     = &(config->tls);
	hocon_read_bool(http_req_tls, enable, jso_http_req_tls);
	hocon_read_str(http_req_tls, key_password, jso_http_req_tls);
	hocon_read_str(http_req_tls, keyfile, jso_http_req_tls);
	hocon_read_str(http_req_tls, keyfile, jso_http_req_tls);
	hocon_read_str(http_req_tls, certfile, jso_http_req_tls);
	hocon_read_str_base(
	    http_req_tls, cafile, "cacertfile", jso_http_req_tls);
}

static void
conf_auth_http_parse_ver2(conf *config, cJSON *jso)
{
	conf_auth_http *auth_http = &(config->auth_http);

	hocon_read_bool(auth_http, enable, jso);
	char *timeout = cJSON_GetStringValue(hocon_get_obj("timeout", jso));
	char *connect_timeout =
	    cJSON_GetStringValue(hocon_get_obj("connect_timeout", jso));
	get_time(timeout, &auth_http->timeout);
	get_time(connect_timeout, &auth_http->timeout);
	hocon_read_num(auth_http, pool_size, jso);

	conf_auth_http_req *auth_http_req     = &(auth_http->auth_req);
	cJSON *             jso_auth_http_req = hocon_get_obj("auth_req", jso);
	conf_auth_http_req_parse_ver2(auth_http_req, jso_auth_http_req);

	conf_auth_http_req *auth_http_super_req = &(auth_http->super_req);
	cJSON *jso_auth_http_super_req = hocon_get_obj("super_req", jso);
	conf_auth_http_req_parse_ver2(
	    auth_http_super_req, jso_auth_http_super_req);
#ifdef ACL_SUPP
	conf_auth_http_req *auth_http_acl_req = &(auth_http->acl_req);
	cJSON *jso_auth_http_acl_req          = hocon_get_obj("acl_req", jso);
	conf_auth_http_req_parse_ver2(
	    auth_http_acl_req, jso_auth_http_acl_req);
#endif
	return;
}

static void
conf_bridge_connector_parse_ver2(conf_bridge_node *node, cJSON *jso_connector)
{
	hocon_read_str_base(node, address, "server", jso_connector);
	hocon_read_num(node, proto_ver, jso_connector);
	hocon_read_str(node, clientid, jso_connector);
	hocon_read_time(node, keepalive, jso_connector);
	hocon_read_bool(node, clean_start, jso_connector);
	hocon_read_str(node, username, jso_connector);
	hocon_read_str(node, password, jso_connector);
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
	    node, qconnect_timeout, "quic_handshake_timeout", jso_bridge_node);
	hocon_read_bool_base(node, hybrid, "hybird_bridging", jso_bridge_node);
	char *cc = cJSON_GetStringValue(
	    cJSON_GetObjectItem(jso_bridge_node, "congestion_control"));
	if (NULL != cc) {
		if (0 == nng_strcasecmp(cc, "bbr")) {
			node->qcongestion_control = 1;
		} else if (0 == nng_strcasecmp(cc, "cubic")) {
			node->qcongestion_control = 0;
		} else {
			node->qcongestion_control = 1;
			log_warn("unsupport congestion control "
			         "algorithm, use "
			         "default bbr!");
		}
	} else {
		node->qcongestion_control = 1;
		log_warn("Unsupport congestion control algorithm, use "
		         "default bbr!");
	}
}
#endif

static void
conf_bridge_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *bridge_mqtt_nodes = hocon_get_obj("bridges.mqtt.nodes", jso);
	cJSON *bridge_mqtt_node  = NULL;

	cJSON *bridge_mqtt_sqlite  = hocon_get_obj("bridges.mqtt.sqlite", jso);
	conf_sqlite *bridge_sqlite = &(config->bridge.sqlite);
	hocon_read_bool(bridge_sqlite, enable, bridge_mqtt_sqlite);
	hocon_read_num(bridge_sqlite, disk_cache_size, bridge_mqtt_sqlite);
	hocon_read_num(bridge_sqlite, flush_mem_threshold, bridge_mqtt_sqlite);
	hocon_read_num(bridge_sqlite, resend_interval, bridge_mqtt_sqlite);
	hocon_read_str(bridge_sqlite, mounted_file_path, bridge_mqtt_sqlite);

	config->bridge.count = cJSON_GetArraySize(bridge_mqtt_nodes);
	config->bridge.nodes = NULL;
	cJSON_ArrayForEach(bridge_mqtt_node, bridge_mqtt_nodes)
	{
		conf_bridge_node *node = NNI_ALLOC_STRUCT(node);
		hocon_read_str(node, name, bridge_mqtt_node);
		hocon_read_bool(node, enable, bridge_mqtt_node);
		cJSON *jso_connector =
		    hocon_get_obj("connector", bridge_mqtt_node);
		conf_bridge_connector_parse_ver2(node, jso_connector);
		hocon_read_str_arr(node, forwards, bridge_mqtt_node);
		node->forwards_count = cvector_size(node->forwards);
		node->sqlite         = bridge_sqlite;

#if defined(SUPP_QUIC)
		conf_bridge_quic_parse_ver2(node, bridge_mqtt_node);
#endif

		cJSON *subscriptions =
		    hocon_get_obj("subscription", bridge_mqtt_node);
		node->sub_count = cJSON_GetArraySize(subscriptions);
		node->sub_list =
		    NNI_ALLOC_STRUCTS(node->sub_list, node->sub_count);
		subscribe *slist        = node->sub_list;
		cJSON *    subscription = NULL;
		int        i            = 0;
		cJSON_ArrayForEach(subscription, subscriptions)
		{
			subscribe *s = &slist[i++];
			hocon_read_str(s, topic, subscription);
			hocon_read_num(s, qos, subscription);
			s->topic_len = strlen(s->topic);
		}

		hocon_read_num(node, parallel, bridge_mqtt_node);
		cJSON *bridge_mqtt_node_tls =
		    hocon_get_obj("ssl", bridge_mqtt_node);
		conf_tls *bridge_node_tls = &(node->tls);
		conf_tls_parse_ver2_base(bridge_node_tls, bridge_mqtt_node_tls);
		cvector_push_back(config->bridge.nodes, node);
		config->bridge_mode |= node->enable;
	}

	return;
}

#if defined(SUPP_AWS_BRIDGE)
static void
conf_aws_bridge_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *bridge_aws_nodes = hocon_get_obj("bridges.aws.nodes", jso);
	cJSON *bridge_aws_node  = NULL;

	config->aws_bridge.count = cJSON_GetArraySize(bridge_aws_nodes);
	config->aws_bridge.nodes = NULL;
	cJSON_ArrayForEach(bridge_aws_node, bridge_aws_nodes)
	{
		conf_bridge_node *node = NNI_ALLOC_STRUCT(node);
		hocon_read_str(node, name, bridge_aws_node);
		hocon_read_bool(node, enable, bridge_aws_node);

		cJSON *jso_connector = hocon_get_obj("connector", bridge_aws_node);
		conf_bridge_connector_parse_ver2(node, jso_connector);

		if (node->address) {
			char *p = NULL;
			if (NULL != (p = strchr(node->address, ':'))) {
				*p = '\0';
				node->host = nng_strdup(node->address);
				node->port = atoi(++p);
				*(p-1) = ':';
			}
		}

		hocon_read_str_arr(node, forwards, bridge_aws_node);
		node->forwards_count = cvector_size(node->forwards);

		cJSON *subscriptions =
		    hocon_get_obj("subscription", bridge_aws_node);
		node->sub_count = cJSON_GetArraySize(subscriptions);
		node->sub_list =
		    NNI_ALLOC_STRUCTS(node->sub_list, node->sub_count);
		subscribe *slist        = node->sub_list;
		cJSON *    subscription = NULL;
		int        i            = 0;
		cJSON_ArrayForEach(subscription, subscriptions)
		{
			subscribe *s = &slist[i++];
			hocon_read_str(s, topic, subscription);
			hocon_read_num(s, qos, subscription);
			s->topic_len = strlen(s->topic);
		}

		hocon_read_num(node, parallel, bridge_aws_node);
		cJSON *bridge_aws_node_tls =
		    hocon_get_obj("ssl", bridge_aws_node);
		conf_tls *bridge_node_tls = &(node->tls);
		hocon_read_bool(bridge_node_tls, enable, bridge_aws_node_tls);
		hocon_read_str(
		    bridge_node_tls, key_password, bridge_aws_node_tls);
		hocon_read_str(bridge_node_tls, keyfile, bridge_aws_node_tls);
		hocon_read_str(bridge_node_tls, keyfile, bridge_aws_node_tls);
		hocon_read_str(bridge_node_tls, certfile, bridge_aws_node_tls);
		hocon_read_str_base(bridge_node_tls, cafile, "cacertfile",
		    bridge_aws_node_tls);

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
	hocon_read_str_base(cr, sqlite_db, "path", jso_rule_sqlite);

	if (cJSON_IsTrue(cJSON_GetObjectItem(jso_rule_sqlite, "enabled"))) {
		cr->option |= RULE_ENG_SDB;
	}
	cJSON *jso_rules = hocon_get_obj("rules", jso_rule_sqlite);
	cJSON *jso_rule  = NULL;

	cJSON_ArrayForEach(jso_rule, jso_rules)
	{

		rule r = { 0 };
		hocon_read_bool(&r, enabled, jso_rule);
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

	cJSON *jso_rule_repub = hocon_get_obj("rules.repub", jso);

	if (cJSON_IsTrue(cJSON_GetObjectItem(jso_rule_repub, "enabled"))) {
		cr->option |= RULE_ENG_RPB;
	}

	jso_rules = hocon_get_obj("rules", jso_rule_repub);
	jso_rule  = NULL;

	cJSON_ArrayForEach(jso_rule, jso_rules)
	{

		rule     re    = { 0 };
		repub_t *repub = NNI_ALLOC_STRUCT(repub);
		hocon_read_bool_base(&re, enabled, "enable", jso_rule);
		hocon_read_str_base(&re, raw_sql, "sql", jso_rule);
		hocon_read_str(repub, address, jso_rule);
		hocon_read_str(repub, topic, jso_rule);
		hocon_read_num(repub, proto_ver, jso_rule);
		hocon_read_str(repub, clientid, jso_rule);
		hocon_read_num(repub, keepalive, jso_rule);
		hocon_read_bool(repub, clean_start, jso_rule);
		hocon_read_str(repub, username, jso_rule);
		hocon_read_str(repub, password, jso_rule);

		rule_sql_parse(cr, re.raw_sql);

		cr->rules[cvector_size(cr->rules) - 1].repub = repub;
		// NNI_ALLOC_STRUCT(repub);
		// memcpy(cr->rules[cvector_size(cr->rules) - 1].repub, repub,
		//     sizeof(*repub));
		cr->rules[cvector_size(cr->rules) - 1].forword_type =
		    RULE_FORWORD_REPUB;
		cr->rules[cvector_size(cr->rules) - 1].raw_sql = re.raw_sql;
		cr->rules[cvector_size(cr->rules) - 1].enabled = re.enabled;
		cr->rules[cvector_size(cr->rules) - 1].rule_id =
		    rule_generate_rule_id();

		// NNI_FREE_STRUCT(repub);
	}

	cJSON *jso_rule_mysql = hocon_get_obj("rules.mysql", jso);
	if (cJSON_IsTrue(cJSON_GetObjectItem(jso_rule_mysql, "enabled"))) {
		cr->option |= RULE_ENG_MDB;
	}

	hocon_read_str_base(cr, mysql_db, "table", jso_rule_mysql);
	jso_rules = hocon_get_obj("rules", jso_rule_mysql);
	jso_rule  = NULL;

	cJSON_ArrayForEach(jso_rule, jso_rules)
	{
		rule r = { 0 };

		hocon_read_bool(&r, enabled, jso_rule);
		hocon_read_str_base(&r, raw_sql, "sql", jso_rule);
		rule_mysql *mysql = NNI_ALLOC_STRUCT(mysql);

		rule_sql_parse(cr, r.raw_sql);

		cr->rules[cvector_size(cr->rules) - 1].mysql = mysql;
		// NNI_ALLOC_STRUCT(mysql);
		// memcpy(cr->rules[cvector_size(cr->rules) - 1].mysql, mysql,
		//     sizeof(*mysql));
		hocon_read_str(cr->rules[cvector_size(cr->rules) - 1].mysql,
		    host, jso_rule);
		hocon_read_str(cr->rules[cvector_size(cr->rules) - 1].mysql,
		    table, jso_rule);
		hocon_read_str(cr->rules[cvector_size(cr->rules) - 1].mysql,
		    username, jso_rule);
		hocon_read_str(cr->rules[cvector_size(cr->rules) - 1].mysql,
		    password, jso_rule);

		cr->rules[cvector_size(cr->rules) - 1].forword_type =
		    RULE_FORWORD_MYSOL;
		cr->rules[cvector_size(cr->rules) - 1].raw_sql = r.raw_sql;
		cr->rules[cvector_size(cr->rules) - 1].enabled = r.enabled;
		cr->rules[cvector_size(cr->rules) - 1].rule_id =
		    rule_generate_rule_id();

		// NNI_FREE_STRUCT(mysql);
	}

	char *rule_option =
	    cJSON_GetStringValue(hocon_get_obj("rules.option", jso));
	if (NULL != rule_option) {
		if (0 != nni_strcasecmp(rule_option, "ON")) {
			if (0 != nni_strcasecmp(rule_option, "OFF")) {
				log_error("Unsupported option:%s\nrule"
				          "option only support ON/OFF",
				    rule_option);
			} else {
				cr->option = 0;
			}
		}
	}

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

	hocon_read_bool(acl, enable, jso);

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
#endif
}

static void
conf_authorization_prase_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_auth_sources = hocon_get_obj("authorization.sources", jso);
	if (!cJSON_IsArray(jso_auth_sources)) {
		log_error("Read config nanomq authorization.sources failed!");
		return;
	}

	cJSON *auth_item;

	cJSON_ArrayForEach(auth_item, jso_auth_sources)
	{
		cJSON *type_obj = cJSON_GetObjectItem(auth_item, "type");
		if (cJSON_IsString(type_obj)) {
			char *type_str = cJSON_GetStringValue(type_obj);
			if (type_str != NULL) {
				if (strcmp(type_str, "simple") == 0) {
					conf_auth_parse_ver2(
					    config, auth_item);
				} else if (strcmp(type_str, "file") == 0) {
					conf_acl_parse_ver2(config, auth_item);
				} else if (strcmp(type_str, "http") == 0) {
					conf_auth_http_parse_ver2(
					    config, auth_item);
				} else {
					log_error("Read unsupported "
					          "authorization type");
				}
			}
		}
	}
}

void
conf_parse_ver2(conf *config)
{
	const char *conf_path = config->conf_file;
	if (conf_path == NULL || !nano_file_exists(conf_path)) {
		if (!nano_file_exists(CONF_PATH_NAME)) {
			log_warn("Configure file [%s] or [%s] not found or "
			          "unreadable",
			    conf_path, CONF_PATH_NAME);
			return;
		} else {
			conf_path = CONF_PATH_NAME;
		}
	}

	cJSON *jso = hocon_parse_file(conf_path);
	if (NULL != jso) {
		conf_basic_parse_ver2(config, jso);
		conf_sqlite_parse_ver2(config, jso);
		conf_tls_parse_ver2(config, jso);
#if defined(ENABLE_LOG)
		conf_log_parse_ver2(config, jso);
#endif
		conf_webhook_parse_ver2(config, jso);
		conf_authorization_prase_ver2(config, jso);
		conf_bridge_parse_ver2(config, jso);
#if defined(SUPP_AWS_BRIDGE)
		conf_aws_bridge_parse_ver2(config, jso);
#endif

#if defined(SUPP_RULE_ENGINE)
		conf_rule_parse_ver2(config, jso);
#endif

		cJSON_Delete(jso);
	}

	return;
}

void
conf_gateway_parse_ver2(zmq_gateway_conf *config)
{
	const char *dest_path = config->path;

	if (dest_path == NULL || !nano_file_exists(dest_path)) {
		if (!nano_file_exists(CONF_GATEWAY_PATH_NAME)) {
			log_debug("Configure file [%s] or [%s] not found or "
			          "unreadable\n",
			    dest_path, CONF_GATEWAY_PATH_NAME);
			return;
		} else {
			dest_path = CONF_GATEWAY_PATH_NAME;
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

	cJSON_Delete(jso);

	// printf_gateway_conf(config);

	return;
}