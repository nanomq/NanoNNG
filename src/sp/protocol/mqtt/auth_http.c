#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/supplemental/http/http.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/hash_table.h"
#include <stdarg.h>
#include <stdio.h>

struct auth_http_params {
	const char *access; // (1 - subscribe, 2 - publish)
	const char *username;
	const char *clientid;
	const char *ipaddress;
	const char *protocol;
	const char *password;
	const char *sockport;
	const char *common;
	const char *subject;
	const char *mountpoint;
	const char *topic;
};

typedef struct auth_http_params auth_http_params;

static size_t
str_append(char **dest, const char *str)
{
	char *old_str = *dest == NULL ? "" : (*dest);
	char *new_str =
	    calloc(strlen(old_str) + strlen(str) + 1, sizeof(char));

	strcat(new_str, old_str);
	strcat(new_str, str == NULL ? "" : str);

	if (*dest) {
		free(*dest);
	}
	*dest = new_str;
	return strlen(new_str);
}

static void
set_data(
    nng_http_req *req, conf_auth_http_req *req_conf, auth_http_params *params)
{
	char *req_data     = NULL;
	char *content_type = "application/x-www-form-urlencoded";

	if (req_conf->header_count == 0) {
		log_error("No headers found in request configuration");
		return;
	}

	for (size_t i = 0; i < req_conf->header_count; i++) {
		if (nni_strcasecmp(req_conf->headers[i]->key, "Content-Type") ==
		    0) {
			content_type = req_conf->headers[i]->value;
			continue;
		}
		nng_http_req_add_header(req, req_conf->headers[i]->key,
		    req_conf->headers[i]->value);
	}

	if (nni_strcasecmp(content_type, "application/json") == 0 &&
	    nni_strcasecmp(req_conf->method, "get") == 0) {
		content_type = "application/x-www-form-urlencoded";
	}

	if (nni_strcasecmp(content_type, "application/json") == 0) {
		cJSON *obj = cJSON_CreateObject();
		for (size_t i = 0; i < req_conf->param_count; i++) {
			switch (req_conf->params[i]->type) {
			case ACCESS:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name, params->access);
				break;
			case USERNAME:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name,
				    params->username);
				break;
			case CLIENTID:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name,
				    params->clientid);
				break;
			case IPADDRESS:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name,
				    params->ipaddress);
				break;
			case PROTOCOL:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name,
				    params->protocol);
				break;
			case PASSWORD:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name,
				    params->password);
				break;
			case SOCKPORT:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name,
				    params->sockport);
				break;
			case COMMON_NAME:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name, params->common);
				break;
			case SUBJECT:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name,
				    params->subject);
				break;
			case TOPIC:
				cJSON_AddStringToObject(obj,
				    req_conf->params[i]->name, params->topic);
				break;
			default:
				break;
			}
		}
		req_data = cJSON_PrintUnformatted(obj);
		cJSON_Delete(obj);
	} else {
		for (size_t i = 0; i < req_conf->param_count; i++) {
			switch (req_conf->params[i]->type) {
			case ACCESS:
				if (params->access) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(&req_data, params->access);
					str_append(&req_data, "&");
				}
				break;
			case USERNAME:
				if (params->username) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(
					    &req_data, params->username);
					str_append(&req_data, "&");
				}
				break;
			case CLIENTID:
				if (params->clientid) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(
					    &req_data, params->clientid);
					str_append(&req_data, "&");
				}
				break;
			case IPADDRESS:
				if (params->ipaddress) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(
					    &req_data, params->ipaddress);
					str_append(&req_data, "&");
				}
				break;
			case PROTOCOL:
				if (params->protocol) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(
					    &req_data, params->protocol);
					str_append(&req_data, "&");
				}
				break;
			case PASSWORD:
				if (params->password) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(
					    &req_data, params->password);
					str_append(&req_data, "&");
				}
				break;
			case SOCKPORT:
				if (params->sockport) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(
					    &req_data, params->sockport);
					str_append(&req_data, "&");
				}
				break;
			case COMMON_NAME:
				if (params->common) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(&req_data, params->common);
					str_append(&req_data, "&");
				}
				break;
			case SUBJECT:
				if (params->subject) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(&req_data, params->subject);
					str_append(&req_data, "&");
				}
				break;
			case TOPIC:
				if (params->topic) {
					str_append(&req_data,
					    req_conf->params[i]->name);
					str_append(&req_data, "=");
					str_append(&req_data, params->topic);
					str_append(&req_data, "&");
				}
				break;
			default:
				break;
			}
		}
		if (req_data != NULL &&
		    req_data[strlen(req_data) - 1] == '&') {
			req_data[strlen(req_data) - 1] = '\0';
		}
	}

	nng_http_req_add_header(req, "Content-Type", content_type);
	nng_http_req_set_method(req, req_conf->method);

	if (nni_strcasecmp(req_conf->method, "post") == 0 ||
	    nni_strcasecmp(req_conf->method, "put") == 0) {
		if (req_data && req_data[0] != '\0') {
			nng_http_req_copy_data(
			    req, req_data, strlen(req_data));
		}
	} else if (req_data && req_data[0] != '\0') {
		const char *base_uri = nng_http_req_get_uri(req);
		size_t      uri_len  = strlen(base_uri) + strlen(req_data) + 2;
		char *      uri      = nng_alloc(uri_len);
		snprintf(uri, uri_len, "%s?%s", base_uri, req_data);
		nng_http_req_set_uri(req, uri);
		nng_free(uri, uri_len);
	}

	if (req_data) {
		free(req_data);
	}
}

static int
send_request(conf_auth_http *conf, conf_auth_http_req *conf_req,
    auth_http_params *params)
{
	nng_http_client *client = NULL;
	nng_http_conn *  conn   = NULL;
	nng_url *        url    = NULL;
	nng_aio *        aio    = NULL;
	nng_http_req *   req    = NULL;
	nng_http_res *   res    = NULL;
	int              status = 0;
	int              rv;

	nng_mtx_lock(conf_req->mtx);
	if (((rv = nng_url_parse(&url, conf_req->url)) != 0) ||
	    ((rv = nng_http_client_alloc(&client, url)) != 0) ||
	    ((rv = nng_http_req_alloc(&req, url)) != 0) ||
	    ((rv = nng_http_res_alloc(&res)) != 0) ||
	    ((rv = nng_aio_alloc(&aio, NULL, NULL)) != 0)) {
		goto out;
	}

	// Start connection process...
	nng_aio_set_timeout(aio, conf->connect_timeout * 1000);
	nng_http_client_connect(client, aio);

	// Wait for it to finish.
	// TODO It could cause some problems.
	nng_aio_wait(aio);
	if ((rv = nng_aio_result(aio)) != 0) {
		log_error("Connect failed: %s\n", nng_strerror(rv));
		goto out;
	}

	// Get the connection, at the 0th output.
	conn = nng_aio_get_output(aio, 0);
	// Request is already set up with URL, and for GET via HTTP/1.1.
	// The Host: header is already set up too.
	set_data(req, conf_req, params);
	// Send the request, and wait for that to finish.
	nng_http_conn_write_req(conn, req, aio);
	nng_aio_set_timeout(aio, conf->timeout * 1000);
	nng_aio_wait(aio);

	if ((rv = nng_aio_result(aio)) != 0) {
		log_error("Write req failed: %s", nng_strerror(rv));
		goto out;
	}

	// Read a response.
	nng_aio_set_timeout(aio, conf->timeout * 1000);
	nng_http_conn_read_res(conn, res, aio);
	nng_aio_wait(aio);

	if ((rv = nng_aio_result(aio)) != 0) {
		log_error("Read response: %s", nng_strerror(rv));
		goto out;
	}

	if ((status = nng_http_res_get_status(res)) != NNG_HTTP_STATUS_OK) {
		log_error("HTTP Server Responded: %d %s",
		    nng_http_res_get_status(res),
		    nng_http_res_get_reason(res));
		goto out;
	}

out:
	if (url) {
		nng_url_free(url);
	}
	if (req) {
		nng_http_req_free(req);
	}
	if (res) {
		nng_http_res_free(res);
	}
	if (conn) {
		nng_http_conn_close(conn);
	}
	if (client) {
		nng_http_client_free(client);
	}
	if (aio) {
		nng_aio_free(aio);
	}
	nng_mtx_unlock(conf_req->mtx);
	return status;
}

/**
 * NNG_HTTP_STATUS_OK returns CONNACK
 * otherwise disconnect
 * */
int
nmq_auth_http_connect(conn_param *cparam, conf_auth_http *conf)
{
	if (cparam == NULL) {
		log_error("nmq_auth_http_connect: cparam is NULL");
		return NOT_AUTHORIZED;
	}

	if (conf->enable == false || conf->auth_req.url == NULL) {
		log_info("HTTP Authentication is not enabled!");
		return SUCCESS;
	}

	auth_http_params auth_params = {
		.clientid  = (const char *) conn_param_get_clientid(cparam),
		.username  = (const char *) conn_param_get_username(cparam),
		.password  = (const char *) conn_param_get_password(cparam),
		.ipaddress = (const char *) conn_param_get_ip_addr_v4(cparam),
		.protocol = cparam->protocol,
		.sockport = cparam->sockport,
		.common   = cparam->tls_peer_cn,
		.subject  = cparam->tls_subject,
	};

	int status = send_request(conf, &conf->auth_req, &auth_params);

	return status == NNG_HTTP_STATUS_OK ? SUCCESS : NOT_AUTHORIZED;
}

char *parse_topics(topic_queue *head)
{
	if (head == NULL) {
	    return NULL;
	}
	int total_length = 0;
	topic_queue *current = head;
	if (current->topic == NULL || strlen(current->topic) == 0) {
		log_error("topic is empty");
		return NULL;
	}
	while (current != NULL) {
	    total_length += strlen(current->topic);
		total_length += 1; // for ','
	    current = current->next;
	}
	char *result = (char *)malloc(total_length + 1);
	if (result == NULL) {
		log_error("malloc failed");
		return NULL;
	}
	current = head;
	result[0] = '\0';
	while (current != NULL) {
	    strcat(result, current->topic);
	    strcat(result, ",");
	    current = current->next;
	}
	if (strlen(result) > 0) {
	    result[strlen(result) - 1] = '\0';
	}
	return result;
}

int
nmq_auth_http_sub_pub(
    conn_param *cparam, bool is_sub, topic_queue *topics, conf_auth_http *conf)
{
	if (conf->enable == false ||
	    (conf->super_req.url == NULL && conf->acl_req.url == NULL)) {
		return SUCCESS;
	}

	char *topic_str = parse_topics(topics);
	if (topic_str == NULL) {
		log_warn("Parsing topic failed for ACL");
		return SUCCESS;
	}

	auth_http_params auth_params = {
		.clientid  = (const char *) conn_param_get_clientid(cparam),
		.username  = (const char *) conn_param_get_username(cparam),
		.password  = (const char *) conn_param_get_password(cparam),
		.access    = is_sub ? "1" : "2",
		.topic     = topic_str,
		.ipaddress = conn_param_get_ip_addr_v4(cparam),
		// TODO incompleted fields
		// .mountpoint = ,
		.protocol = cparam->protocol,
		.sockport = cparam->sockport,
		.common   = cparam->tls_peer_cn,
		.subject  = cparam->tls_subject,
	};
	int status = NNG_HTTP_STATUS_OK;

	// The key of ACL Cache Map is hash(clientid,username,password,access,topic,ip)
	// The ACL Cache Map will be reset after every interval.
	char *auth_params_clientid = "null";
	if (auth_params.clientid)
		auth_params_clientid = (char*) auth_params.clientid;
	char *auth_params_username = "null";
	if (auth_params.username)
		auth_params_username = (char*) auth_params.username;
	char *auth_params_password = "null";
	if (auth_params.password)
		auth_params_password = (char*) auth_params.password;
	char *auth_params_ipaddress = "null";
	if (auth_params.ipaddress)
		auth_params_ipaddress = (char*) auth_params.ipaddress;

	char acl_cache_k_str[1024];
	snprintf(acl_cache_k_str, 1024, "ACLK%s,%s,%s,%s,%s,%s",
		auth_params_clientid, auth_params_username, auth_params_password,
		auth_params.access, topic_str, auth_params_ipaddress);
	acl_cache_k_str[1023] = '\0'; // Avoid StackOverFlow
	uint32_t acl_cache_k = DJBHash(acl_cache_k_str);

	if (conf->super_req.enable) {
		if (conf->cache_ttl > 0) {
			nng_mtx_lock(conf->acl_cache_mtx);
			void *acl_cache_v = nng_id_get(
					conf->acl_cache_map, (uint64_t)acl_cache_k);
			nng_mtx_unlock(conf->acl_cache_mtx);
			if (acl_cache_v != NULL) {
				nni_free(topic_str, strlen(topic_str) + 1);
				return SUCCESS; // cache hit
			}
		}

		status = send_request(conf, &conf->super_req, &auth_params);
		if (status == NNG_HTTP_STATUS_OK) {
			if (conf->cache_ttl > 0) {
				log_debug("acl passed, add cache %ld, %s",
						acl_cache_k, acl_cache_k_str);
				nng_mtx_lock(conf->acl_cache_mtx);
				nng_id_set(conf->acl_cache_map,
						(uint64_t)acl_cache_k, (void*)conf);
				nng_mtx_unlock(conf->acl_cache_mtx);
			}
			nni_free(topic_str, strlen(topic_str) + 1);
			return SUCCESS;
		}
	}

	if (conf->acl_req.enable) {
		if (conf->cache_ttl > 0) {
			nng_mtx_lock(conf->acl_cache_mtx);
			void *acl_cache_v = nng_id_get(
					conf->acl_cache_map, (uint64_t)acl_cache_k);
			nng_mtx_unlock(conf->acl_cache_mtx);
			if (acl_cache_v != NULL) {
				nni_free(topic_str, strlen(topic_str) + 1);
				return SUCCESS; // cache hit
			}
		}

		status = conf->acl_req.url == NULL
		    ? NNG_HTTP_STATUS_OK
		    : send_request(conf, &conf->acl_req, &auth_params);
		if (status == NNG_HTTP_STATUS_OK) {
			if (conf->cache_ttl > 0) {
				log_debug("acl passed, add cache %ld, %s",
						acl_cache_k, acl_cache_k_str);
				nng_mtx_lock(conf->acl_cache_mtx);
				nng_id_set(conf->acl_cache_map,
						(uint64_t)acl_cache_k, (void*)conf);
				nng_mtx_unlock(conf->acl_cache_mtx);
			}
		}
	}
	nni_free(topic_str, strlen(topic_str) + 1);
	return status == NNG_HTTP_STATUS_OK ? SUCCESS : NOT_AUTHORIZED;
}
