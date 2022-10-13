#include "nng/supplemental/nanolib/conf.h"
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/hocon.h"
#include <ctype.h>
#include <string.h>

// Read json value into struct
// use same struct fields and json keys
#define hocon_read_str_base(structure, field, key, jso)          \
	do {                                                     \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);  \
		if (NULL == jso_key) {                           \
			log_error("Read config %s failed!", key); \
			break;                                   \
		}                                                \
		switch (jso_key->type) {                         \
		case cJSON_String:                               \
			(structure)->field =                     \
			    nng_strdup(jso_key->valuestring);    \
			break;                                   \
		default:                                         \
			break;                                   \
		}                                                \
	} while (0);

#define hocon_read_bool_base(structure, field, key, jso)            \
	do {                                                        \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);     \
		if (NULL == jso_key) {                              \
			log_error("Read config %s failed!", key); \
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

#define hocon_read_num_base(structure, field, key, jso)             \
	do {                                                        \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);     \
		if (NULL == jso_key) {                              \
			log_error("Read config %s failed!", key); \
			break;                                      \
		}                                                   \
		switch (jso_key->type) {                            \
		case cJSON_Number:                                  \
			(structure)->field = jso_key->valueint;     \
			break;                                      \
		default:                                            \
			break;                                      \
		}                                                   \
	} while (0);

#define hocon_read_str_arr_base(structure, field, key, jso)              \
	do {                                                                 \
		cJSON *jso_arr = cJSON_GetObjectItem(jso, key);           \
		if (NULL == jso_arr) {                                       \
			log_error("Read config %s failed!", key); \
			break;                                              	\
		}                                                            \
		cJSON *elem = NULL;                                          \
		cJSON_ArrayForEach(elem, jso_arr)                            \
		{                                                            \
			switch (elem->type) {                                \
			case cJSON_String:                                   \
				cvector_push_back((structure)->field,             \
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
#define hocon_read_str(structure, key, jso) hocon_read_str_base(structure, key, #key, jso)
#define hocon_read_num(structure, key, jso) hocon_read_num_base(structure, key, #key, jso)
#define hocon_read_bool(structure, key, jso) hocon_read_bool_base(structure, key, #key, jso)
#define hocon_read_str_arr(structure, key, jso) hocon_read_str_arr_base(structure, key, #key, jso)

static char **string_split(char *str, char sp)
{
	char **ret = NULL;
	char *p = str;
	char *p_b = p;
	while (NULL != (p = strchr(p, sp))) {
		*p++ = '\0';
		cvector_push_back(ret, p_b);
	}
	cvector_push_back(ret, p_b);
	return ret;
}

cJSON *hocon_get_obj(char *key, cJSON *jso)
{
	cJSON *ret = jso;
	char **str_vec = string_split(key, '.');
	
	for (size_t i = 0; cvector_size(str_vec); i++) {
		ret = cJSON_GetObjectItem(ret, str_vec[i]);
	}

	return ret;
}

static void conf_basic_parse_ver2(conf *config, cJSON *jso)
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
		config->http_server.auth_type = BASIC;
	} else if ((nni_strcasecmp("jwt", auth_type_value) == 0)) {
		config->http_server.auth_type = JWT;
	} else {
		config->http_server.auth_type = NONE_AUTH;
	}

	conf_jwt *jwt = &(http_server->jwt);

	cJSON *jso_pub_key_file = hocon_get_obj("jwt.public", jso_http_server);
	cJSON *jso_pri_key_file =
	    hocon_get_obj("jwt.private", jso_http_server);
	hocon_read_str_base(jwt, public_keyfile, "keyfile", jso_pub_key_file);
	hocon_read_str_base(jwt, private_keyfile, "keyfile", jso_pri_key_file);

    return;
}

static void conf_tls_parse_ver2(conf *config, cJSON *jso)
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

static void conf_sqlite_parse_ver2(conf *config, cJSON *jso)
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

static void conf_log_parse_ver2(conf *config, cJSON *jso)
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
	log->level = log_level_num(cJSON_GetStringValue(jso_log_level));

	hocon_read_str(log, dir, jso_log);
	hocon_read_str(log, file, jso_log);
	cJSON *jso_log_rotation = hocon_get_obj("rotation", jso_log);
	hocon_read_str_base(log, rotation_sz_str, "size", jso_log_rotation);
	size_t num           = 0;
	char   unit[10]      = { 0 };
	int    res = sscanf(log->rotation_sz_str, "%zu%s", &num, unit);
	if (res == 2) {
		if (nni_strcasecmp(unit, "KB") == 0) {
			log->rotation_sz = num * 1024;
		} else if (nni_strcasecmp(unit, "MB") == 0) {
			log->rotation_sz = num * 1024 * 1024;
		} else if (nni_strcasecmp(unit, "GB") == 0) {
			log->rotation_sz =
			    num * 1024 * 1024 * 1024;
		}
	}

	hocon_read_num_base(log, rotation_count, "count", jso_log_rotation);

    return;
}


static void conf_webhook_parse_ver2(conf *config, cJSON *jso)
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
		conf_http_header web_hook_http_header = { 0 };
		web_hook_http_header.key = nng_strdup(webhook_header->string);
		web_hook_http_header.value =
		    nng_strdup(webhook_header->valuestring);
		// TODO FIX
		cvector_push_back(webhook->headers, &web_hook_http_header);
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

    return;
}

static void conf_auth_parse_ver2(conf *config, cJSON *jso)
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