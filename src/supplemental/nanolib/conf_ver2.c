#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/hocon.h"
#include "nng/supplemental/nanolib/log.h"
#include <ctype.h>
#include <string.h>

// Read json value into struct
// use same struct fields and json keys
#define hocon_read_str_base(structure, field, key, jso)           \
	do {                                                      \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);   \
		if (NULL == jso_key) {                            \
			log_error("Read config %s failed!", key); \
			break;                                    \
		}                                                 \
		switch (jso_key->type) {                          \
		case cJSON_String:                                \
            FREE_NONULL((structure)->field);            \
			(structure)->field =                      \
			    nng_strdup(jso_key->valuestring);     \
			break;                                    \
		default:                                          \
			break;                                    \
		}                                                 \
	} while (0);

#define hocon_read_bool_base(structure, field, key, jso)            \
	do {                                                        \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);     \
		if (NULL == jso_key) {                              \
			log_error("Read config %s failed!", key);   \
			break;                                      \
		}                                                   \
		switch (jso_key->type) {                            \
		case cJSON_True:                                    \
			(structure)->field = cJSON_IsTrue(jso_key); \
			break;                                      \
		default:                                            \
			break;                                      \
		}                                                   \
	} while (0);

#define hocon_read_num_base(structure, field, key, jso)           \
	do {                                                      \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);   \
		if (NULL == jso_key) {                            \
			log_error("Read config %s failed!", key); \
			break;                                    \
		}                                                 \
		switch (jso_key->type) {                          \
		case cJSON_Number:                                \
			(structure)->field = jso_key->valueint;   \
			break;                                    \
		default:                                          \
			break;                                    \
		}                                                 \
	} while (0);

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
			switch (elem->type) {                                \
			case cJSON_String:                                   \
				cvector_push_back((structure)->field,        \
				    nng_strdup(cJSON_GetStringValue(elem))); \
				break;                                       \
			default:                                             \
				break;                                       \
			}                                                    \
		}                                                            \
	} while (0);

// Add easier interface
// if struct fields and json key
// are the same use this interface
#define hocon_read_str(structure, key, jso) \
	hocon_read_str_base(structure, key, #key, jso)
#define hocon_read_num(structure, key, jso) \
	hocon_read_num_base(structure, key, #key, jso)
#define hocon_read_bool(structure, key, jso) \
	hocon_read_bool_base(structure, key, #key, jso)
#define hocon_read_str_arr(structure, key, jso) \
	hocon_read_str_arr_base(structure, key, #key, jso)

static char **
string_split(char *str, char sp)
{
	char **ret = NULL;
	char  *p   = str;
	char  *p_b = p;
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

// TODO FREE_NULL
static void
conf_basic_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_nanomq = cJSON_GetObjectItem(jso, "nanomq");
	if (NULL == jso_nanomq) {
		log_error("Read config nanomq failed!");
		return;
	}

	hocon_read_str(config, url, jso_nanomq);
	hocon_read_bool(config, daemon, jso_nanomq);
	hocon_read_num(config, num_taskq_thread, jso_nanomq);
	hocon_read_num(config, max_taskq_thread, jso_nanomq);
	hocon_read_num(config, parallel, jso_nanomq);
	hocon_read_num(config, property_size, jso_nanomq);
	hocon_read_num(config, max_packet_size, jso_nanomq);
	hocon_read_num(config, client_max_packet_size, jso_nanomq);
	hocon_read_num(config, msq_len, jso_nanomq);
	hocon_read_num(config, qos_duration, jso_nanomq);
	hocon_read_num_base(config, backoff, "keepalive_backoff", jso_nanomq);
	hocon_read_bool(config, allow_anonymous, jso_nanomq);

	cJSON *jso_websocket = cJSON_GetObjectItem(jso, "websocket");
	if (NULL == jso_websocket) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}

	conf_websocket *websocket = &(config->websocket);
	hocon_read_bool(websocket, enable, jso_websocket);
	hocon_read_str(websocket, url, jso_websocket);
	hocon_read_str(websocket, tls_url, jso_websocket);

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

	char *auth_type_value = cJSON_GetStringValue(
	    cJSON_GetObjectItem(jso_http_server, "auth_type"));

	if (nni_strcasecmp("basic", auth_type_value) == 0) {
		http_server->auth_type = BASIC;
	} else if ((nni_strcasecmp("jwt", auth_type_value) == 0)) {
		http_server->auth_type = JWT;
	} else {
		http_server->auth_type = NONE_AUTH;
	}

	conf_jwt *jwt = &(http_server->jwt);

	cJSON *jso_pub_key_file = hocon_get_obj("jwt.public", jso_http_server);
	cJSON *jso_pri_key_file =
	    hocon_get_obj("jwt.private", jso_http_server);
	hocon_read_str_base(jwt, public_keyfile, "keyfile", jso_pub_key_file);
	hocon_read_str_base(jwt, private_keyfile, "keyfile", jso_pri_key_file);
	if (file_load_data(
	        jwt->public_keyfile, (void **) &jwt->public_key) > 0) {
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
conf_tls_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_tls = cJSON_GetObjectItem(jso, "tls");
	if (NULL == jso_tls) {
		log_error("Read config tls failed!");
		return;
	}

	conf_tls *tls = &(config->tls);
	hocon_read_bool(tls, enable, jso_tls);
	hocon_read_str(tls, url, jso_tls);
	hocon_read_str(tls, key_password, jso_tls);
	hocon_read_str(tls, keyfile, jso_tls);
	hocon_read_str(tls, certfile, jso_tls);
	hocon_read_str_base(tls, cafile, "cacertfile", jso_tls);
	hocon_read_bool(tls, verify_peer, jso_tls);
	hocon_read_bool_base(tls, set_fail, "fail_if_no_peer_cert", jso_tls);

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

static void
conf_log_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_log = cJSON_GetObjectItem(jso, "log");
	if (NULL == jso_log) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}

	conf_log *log = &(config->log);

	cJSON *jso_log_to     = hocon_get_obj("to", jso_log);
	cJSON *jso_log_to_ele = NULL;
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
	int rv = log_level_num(cJSON_GetStringValue(jso_log_level));
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

static void
conf_web_hook_parse_rules_ver2(conf *config, cJSON *jso)
{
    // TODO
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
		conf_http_header *web_hook_http_header = NNI_ALLOC_STRUCT(web_hook_http_header);
		web_hook_http_header->key = nng_strdup(webhook_header->string);
		web_hook_http_header->value =
		    nng_strdup(webhook_header->valuestring);
		cvector_push_back(webhook->headers, web_hook_http_header);
	}
	webhook->header_count = cvector_size(webhook->headers);

	char *webhook_encoding =
	    cJSON_GetStringValue(cJSON_GetObjectItem(jso_webhook, "encoding"));
	if (nni_strcasecmp(webhook_encoding, "base64") == 0) {
		webhook->encode_payload = base64;
	} else if (nni_strcasecmp(webhook_encoding, "base62") == 0) {
		webhook->encode_payload = base62;
	} else if (nni_strcasecmp(webhook_encoding, "plain") == 0) {
		webhook->encode_payload = plain;
	}

    conf_web_hook_parse_rules_ver2(config, jso);

	return;
}

static void
conf_auth_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_auth = cJSON_GetObjectItem(jso, "auth");
	if (NULL == jso_auth) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}

	conf_auth *auth         = &(config->auths);
	cJSON     *jso_auth_ele = NULL;

	cJSON_ArrayForEach(jso_auth_ele, jso_auth)
	{
		char *auth_username = cJSON_GetStringValue(
		    cJSON_GetObjectItem(jso_auth_ele, "login"));
		cvector_push_back(auth->usernames, nng_strdup(auth_username));
		char *auth_password = cJSON_GetStringValue(
		    cJSON_GetObjectItem(jso_auth_ele, "password"));
		cvector_push_back(auth->passwords, nng_strdup(auth_password));
	}
	auth->count = cvector_size(auth->usernames);

	return;
}

static void
conf_auth_http_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *jso_auth_http = cJSON_GetObjectItem(jso, "auth_http");
	if (NULL == jso_auth_http) {
		log_error("Read config nanomq sqlite failed!");
		return;
	}

	conf_auth_http *auth_http = &(config->auth_http);

	hocon_read_bool(auth_http, enable, jso_auth_http);
	hocon_read_num(auth_http, timeout, jso_auth_http);
	hocon_read_num(auth_http, connect_timeout, jso_auth_http);
	hocon_read_num(auth_http, pool_size, jso_auth_http);
	conf_auth_http_req *auth_http_req = &(auth_http->auth_req);
	cJSON *jso_auth_http_req = hocon_get_obj("auth_req", jso_auth_http);
	hocon_read_str(auth_http_req, url, jso_auth_http_req);
	hocon_read_str(auth_http_req, method, jso_auth_http_req);
	cJSON *jso_auth_http_req_headers =
	    hocon_get_obj("headers", jso_auth_http);
	cJSON *jso_auth_http_req_header = NULL;
	cJSON_ArrayForEach(jso_auth_http_req_header, jso_auth_http_req_headers)
	{
		conf_http_header auth_http_req_header = { 0 };
		auth_http_req_header.key =
		    nng_strdup(jso_auth_http_req_header->string);
		auth_http_req_header.value =
		    nng_strdup(jso_auth_http_req_header->valuestring);
		cvector_push_back(
		    auth_http_req->headers, &auth_http_req_header);
	}
	auth_http_req->header_count = cvector_size(auth_http_req->headers);

	return;
}

static void
conf_bridge_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *bridge_mqtt_nodes = hocon_get_obj("bridge.mqtt.nodes", jso);
	cJSON *bridge_mqtt_node  = NULL;

	config->bridge.count = cJSON_GetArraySize(bridge_mqtt_nodes);
	// allocate memory
	config->bridge.nodes = NULL;
	cJSON_ArrayForEach(bridge_mqtt_node, bridge_mqtt_nodes)
	{
		conf_bridge_node node = { 0 };
		hocon_read_str((&node), name, bridge_mqtt_node);
		hocon_read_str((&node), address, bridge_mqtt_node);
		hocon_read_num((&node), proto_ver, bridge_mqtt_node);
		hocon_read_bool((&node), enable, bridge_mqtt_node);
		hocon_read_str((&node), clientid, bridge_mqtt_node);
		hocon_read_num((&node), keepalive, bridge_mqtt_node);
		hocon_read_bool((&node), clean_start, bridge_mqtt_node);
		hocon_read_str((&node), username, bridge_mqtt_node);
		hocon_read_str((&node), password, bridge_mqtt_node);
		hocon_read_str_arr((&node), forwards, bridge_mqtt_node);

		subscribe *slist = node.sub_list;
		cJSON     *subscriptions =
		    hocon_get_obj("subscription", bridge_mqtt_node);
		cJSON *subscription = NULL;
		cJSON_ArrayForEach(subscription, subscriptions)
		{
			subscribe s = { 0 };
			hocon_read_str((&s), topic, subscription);
			hocon_read_num((&s), qos, subscription);
			s.topic_len = strlen(s.topic);
			cvector_push_back(slist, s);
		}

		hocon_read_num((&node), parallel, bridge_mqtt_node);
		cJSON *bridge_mqtt_node_tls =
		    hocon_get_obj("tls", bridge_mqtt_node);
		conf_tls *bridge_node_tls = &(node.tls);
		hocon_read_bool(bridge_node_tls, enable, bridge_mqtt_node_tls);
		hocon_read_str(
		    bridge_node_tls, key_password, bridge_mqtt_node_tls);
		hocon_read_str(bridge_node_tls, keyfile, bridge_mqtt_node_tls);
		hocon_read_str(bridge_node_tls, keyfile, bridge_mqtt_node_tls);
		hocon_read_str(
		    bridge_node_tls, certfile, bridge_mqtt_node_tls);
		hocon_read_str_base(bridge_node_tls, cafile, "cacertfile",
		    bridge_mqtt_node_tls);

		// TODO FIX
		cvector_push_back(config->bridge.nodes, &node);
	}

	cJSON *bridge_mqtt_sqlite  = hocon_get_obj("bridge.mqtt.sqlite", jso);
	conf_sqlite *bridge_sqlite = &(config->bridge.sqlite);
	hocon_read_bool(bridge_sqlite, enable, bridge_mqtt_sqlite);
	hocon_read_num(bridge_sqlite, disk_cache_size, bridge_mqtt_sqlite);
	hocon_read_num(bridge_sqlite, flush_mem_threshold, bridge_mqtt_sqlite);
	hocon_read_num(bridge_sqlite, resend_interval, bridge_mqtt_sqlite);
	hocon_read_str(bridge_sqlite, mounted_file_path, bridge_mqtt_sqlite);

	return;
}

static void
conf_aws_bridge_parse_ver2(conf *config, cJSON *jso)
{
	cJSON *bridge_aws_nodes = hocon_get_obj("bridge.aws.nodes", jso);
	cJSON *bridge_aws_node  = NULL;

	config->aws_bridge.count = cJSON_GetArraySize(bridge_aws_nodes);
	// allocate memory
	config->aws_bridge.nodes = NULL;
	cJSON_ArrayForEach(bridge_aws_node, bridge_aws_nodes)
	{
		conf_bridge_node node = { 0 };
		hocon_read_str((&node), name, bridge_aws_node);
		hocon_read_str_base((&node), host, "hosts", bridge_aws_node);
		hocon_read_num((&node), port, bridge_aws_node);
		hocon_read_num((&node), proto_ver, bridge_aws_node);
		hocon_read_bool((&node), enable, bridge_aws_node);
		hocon_read_str((&node), clientid, bridge_aws_node);
		hocon_read_num((&node), keepalive, bridge_aws_node);
		hocon_read_bool((&node), clean_start, bridge_aws_node);
		hocon_read_str((&node), username, bridge_aws_node);
		hocon_read_str((&node), password, bridge_aws_node);
		hocon_read_str_arr((&node), forwards, bridge_aws_node);

		subscribe *slist = node.sub_list;
		cJSON     *subscriptions =
		    hocon_get_obj("subscription", bridge_aws_node);
		cJSON *subscription = NULL;
		cJSON_ArrayForEach(subscription, subscriptions)
		{
			subscribe s = { 0 };
			hocon_read_str((&s), topic, subscription);
			hocon_read_num((&s), qos, subscription);
			s.topic_len = strlen(s.topic);
			cvector_push_back(slist, s);
		}

		hocon_read_num((&node), parallel, bridge_aws_node);
		cJSON *bridge_aws_node_tls =
		    hocon_get_obj("tls", bridge_aws_node);
		conf_tls *bridge_node_tls = &(node.tls);
		hocon_read_bool(bridge_node_tls, enable, bridge_aws_node_tls);
		hocon_read_str(
		    bridge_node_tls, key_password, bridge_aws_node_tls);
		hocon_read_str(bridge_node_tls, keyfile, bridge_aws_node_tls);
		hocon_read_str(bridge_node_tls, keyfile, bridge_aws_node_tls);
		hocon_read_str(bridge_node_tls, certfile, bridge_aws_node_tls);
		hocon_read_str_base(bridge_node_tls, cafile, "cacertfile",
		    bridge_aws_node_tls);

		// TODO FIX
		cvector_push_back(config->aws_bridge.nodes, &node);
	}

	return;
}

#if defined(SUPP_RULE_ENGINE)
static void
conf_rule_parse_ver2(conf *config, cJSON *jso)
{

	rule  *rules      = &(config.rule_eng.rules);
	cJSON *rule_sqlte = hocon_get_obj("rules.sqlite", jso);

	hocon_read_str(rule, path, rule_sqlite);
	hocon_read_bool(rule, enabled, rule_sqlite);
	cJSON *jso_rules = hocon_get_obj("rules", rule_sqlite);
	cJSON *jso_rule  = NULL;

	cJSON_ArrayForEach(jso_rule, jso_rules)
	{
		rule r = { 0 };
		hocon_read_bool(r, enabled, jso_rule);
		hocon_read_str(r, sql, jso_rule);
		hocon_read_str(r, table, jso_rule);
		// cvector_push_back(rules, r);
	}

	cJSON *rule_repub = hocon_get_obj("rules.repub", jso);

	hocon_read_bool(rule, enabled, rule_repub);
	jso_rules = hocon_get_obj("rules", rule_repub);
	jso_rule  = NULL;

	cJSON_ArrayForEach(jso_rule, jso_rules)
	{
		rule r = { 0 };
		hocon_read_bool(&r, enabled, jso_rule);
		hocon_read_str(&r, sql, jso_rule);
		hocon_read_str(&r, address, jso_rule);
		hocon_read_str(&r, topic, jso_rule);
		hocon_read_num(&r, proto_ver, jso_rule);
		hocon_read_str(&r, clientid, jso_rule);
		hocon_read_num(&r, keepalive, jso_rule);
		hocon_read_bool(&r, clean_start, jso_rule);
		hocon_read_str(&r, username, jso_rule);
		hocon_read_str(&r, password, jso_rule);
		// cvector_push_back(rules, r);
	}

	cJSON *rule_mysql = hocon_get_obj("rules.mysql", jso);
	hocon_read_bool(rule, enabled, rule_mysql);
	hocon_read_str(rule, name, rule_mysql);
	jso_rules = hocon_get_obj("rules", rule_mysql);
	jso_rule  = NULL;

	cJSON_ArrayForEach(jso_rule, jso_rules)
	{
		rule r = { 0 };
		hocon_read_bool(&r, enabled, jso_rule);
		hocon_read_str(&r, sql, jso_rule);
		hocon_read_str(&r, host, jso_rule);
		hocon_read_str(&r, table, jso_rule);
		hocon_read_str(&r, username, jso_rule);
		hocon_read_str(&r, password, jso_rule);
		// cvector_push_back(rules, r);
	}

	return;
}
#endif

char *
json_buffer_from_fp(FILE *fp)
{

	char  *buffer    = NULL;
	char   buf[8192] = { 0 };
	size_t num;

	while (!feof(fp)) {
		num = fread(buf, sizeof(buf), 1, fp);
		for (size_t i = 0; i < sizeof(buf); i++) {
			cvector_push_back(buffer, buf[i]);
		}
		memset(buf, 0, 8192);
	}

	return buffer;
}

void
conf_parse_ver2(conf *config)
{
	const char *conf_path = config->conf_file;
	if (conf_path == NULL || !nano_file_exists(conf_path)) {
		if (!nano_file_exists(CONF_PATH_NAME)) {
			log_debug("Configure file [%s] or [%s] not found or "
			          "unreadable",
			    conf_path, CONF_PATH_NAME);
			return;
		} else {
			conf_path = CONF_PATH_NAME;
		}
	}

	FILE *fp;
	if ((fp = fopen(conf_path, "r")) == NULL) {
		log_error("File %s open failed", conf_path);
		return;
	}

	char *str = json_buffer_from_fp(fp);
	puts(str);
	if (str != NULL) {

		cJSON *jso = hocon_str_to_json(str);
		char  *ret = cJSON_PrintUnformatted(jso);

		conf_basic_parse_ver2(config, jso);
		conf_sqlite_parse_ver2(config, jso);
		conf_tls_parse_ver2(config, jso);
		conf_log_parse_ver2(config, jso);
		conf_webhook_parse_ver2(config, jso);
		conf_auth_parse_ver2(config, jso);
		conf_auth_http_parse_ver2(config, jso);
		conf_bridge_parse_ver2(config, jso);
		conf_aws_bridge_parse_ver2(config, jso);

#if defined(SUPP_RULE_ENGINE)
		conf_rule_parse_ver2(config, jso);
#endif

		cJSON_free(ret);
		cJSON_Delete(jso);
		cvector_free(str);

	} else {
		log_error("Unable to parse contents of json");
	}
	fclose(fp);

	return;
}