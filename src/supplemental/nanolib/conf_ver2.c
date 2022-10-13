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