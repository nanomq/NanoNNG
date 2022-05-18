#include "cJSON.h"
#include "conf.h"
#include "hash_table.h"
#include "nng/nng.h"
#include "nng/nng_debug.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/supplemental/http/http.h"
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

	for (size_t i = 0; i < req_conf->header_count; i++) {
		if (strcasecmp(req_conf->headers[i]->key, "Content-Type") ==
		    0) {
			content_type = req_conf->headers[i]->value;
			continue;
		}
		nng_http_req_add_header(req, req_conf->headers[i]->key,
		    req_conf->headers[i]->value);
	}

	if (strcasecmp(content_type, "application/json") == 0 &&
	    strcasecmp(req_conf->method, "get") == 0) {
		content_type = "application/x-www-form-urlencoded";
	}

	if (strcasecmp(content_type, "application/json") == 0) {
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

	if (strcasecmp(req_conf->method, "post") == 0 ||
	    strcasecmp(req_conf->method, "put") == 0) {
		nng_http_req_copy_data(req, req_data, strlen(req_data));
	} else {
		size_t uri_len =
		    strlen(nng_http_req_get_uri(req)) + strlen(req_data) + 2;
		char *uri = nng_alloc(uri_len);
		sprintf(uri, "%s?%s", nng_http_req_get_uri(req), req_data);
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
	nng_aio_wait(aio);
	if ((rv = nng_aio_result(aio)) != 0) {
		debug_msg("Connect failed: %s\n", nng_strerror(rv));
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
		debug_msg("Write req failed: %s\n", nng_strerror(rv));
		goto out;
	}

	// Read a response.
	nng_aio_set_timeout(aio, conf->timeout * 1000);
	nng_http_conn_read_res(conn, res, aio);
	nng_aio_wait(aio);

	if ((rv = nng_aio_result(aio)) != 0) {
		debug_msg("Read response: %s\n", nng_strerror(rv));
		goto out;
	}

	if ((status = nng_http_res_get_status(res)) != NNG_HTTP_STATUS_OK) {
		debug_msg("HTTP Server Responded: %d %s\n",
		    nng_http_res_get_status(res),
		    nng_http_res_get_reason(res));
		goto out;
	}

out:
	if (url) {
		nng_url_free(url);
	}
	if (conn) {
		nng_http_conn_close(conn);
	}
	if (client) {
		nng_http_client_free(client);
	}
	if (req) {
		nng_http_req_free(req);
	}
	if (res) {
		nng_http_res_free(res);
	}
	if (aio) {
		nng_aio_free(aio);
	}

	return status;
}

int
nmq_auth_http_connect(conn_param *cparam, conf_auth_http *conf)
{
	if (conf->enable == false || conf->auth_req.url == NULL) {
		return NNG_HTTP_STATUS_OK;
	}

	auth_http_params auth_params = {
		.clientid = (const char *) conn_param_get_clientid(cparam),
		.username = (const char *) conn_param_get_username(cparam),
		.password = (const char *) conn_param_get_password(cparam),
		// TODO incompleted fields
		// .ipaddress = ,
		// .protocol = ,
		// .sockport = ,
		// .common = ,
		// .subject = ,
	};

	int status = send_request(conf, &conf->auth_req, &auth_params);

	return status == NNG_HTTP_STATUS_OK ? SUCCESS : NOT_AUTHORIZED;
}

int
nmq_auth_http_publish(
    conn_param *cparam, bool is_sub, const char *topic, conf_auth_http *conf)
{
	if (conf->enable == false ||
	    (conf->super_req.url == NULL && conf->acl_req.url)) {
		return NNG_HTTP_STATUS_OK;
	}
	auth_http_params auth_params = {
		.clientid = (const char *) conn_param_get_clientid(cparam),
		.username = (const char *) conn_param_get_username(cparam),
		.password = (const char *) conn_param_get_password(cparam),
		.access   = is_sub ? "1" : "2",
		.topic    = topic,
		// TODO incompleted fields
		// .mountpoint = ,
		// .ipaddress = ,
		// .protocol = ,
		// .sockport = ,
		// .common = ,
		// .subject = ,
	};
	int status = 0;
	if (conf->super_req.url) {
		status = send_request(conf, &conf->super_req, &auth_params);
		if (status == NNG_HTTP_STATUS_OK) {
			return status;
		}
	} else {
		status = NNG_HTTP_STATUS_OK;
	}
	status = conf->acl_req.url == NULL
	    ? NNG_HTTP_STATUS_OK
	    : send_request(conf, &conf->acl_req, &auth_params);

	return status == NNG_HTTP_STATUS_OK ? SUCCESS : NOT_AUTHORIZED;
}

int
nmq_auth_http_subscribe(
    conn_param *cparam, bool is_sub, topic_queue *topics, conf_auth_http *conf)
{
	if (conf->enable == false ||
	    (conf->super_req.url == NULL && conf->acl_req.url)) {
		return NNG_HTTP_STATUS_OK;
	}

	char *topic_str = NULL;
	for (topic_queue *tq = topics; tq != NULL; tq = tq->next) {
		str_append(&topic_str, tq->topic);
		str_append(&topic_str, ",");
	}

	if (topic_str != NULL && topic_str[strlen(topic_str) - 1] == ',') {
		topic_str[strlen(topic_str) - 1] = '\0';
	}

	auth_http_params auth_params = {
		.clientid = (const char *) conn_param_get_clientid(cparam),
		.username = (const char *) conn_param_get_username(cparam),
		.password = (const char *) conn_param_get_password(cparam),
		.access   = is_sub ? "1" : "2",
		.topic    = topic_str,
		// TODO incompleted fields
		// .mountpoint = ,
		// .ipaddress = ,
		// .protocol = ,
		// .sockport = ,
		// .common = ,
		// .subject = ,
	};
	int status = 0;
	if (conf->super_req.url) {
		status = send_request(conf, &conf->super_req, &auth_params);
		if (status == NNG_HTTP_STATUS_OK) {
			return status;
		}
	} else {
		status = NNG_HTTP_STATUS_OK;
	}
	status = conf->acl_req.url == NULL
	    ? NNG_HTTP_STATUS_OK
	    : send_request(conf, &conf->acl_req, &auth_params);

	return status == NNG_HTTP_STATUS_OK ? SUCCESS : NOT_AUTHORIZED;
}