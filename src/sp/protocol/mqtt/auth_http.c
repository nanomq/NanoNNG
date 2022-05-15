#include "auth_http.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
	AUTH_REQ,
	SUPER_REQ,
	ACL_REQ,
	REQ_COUNT,
} auth_type;

struct comm_data {
	int                 status;
	conf_auth_http *    config;
	conf_auth_http_req *req;
	auth_http_params *  params;
};

typedef struct comm_data comm_data;

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
send_msg(struct mg_connection *c, conf_auth_http_req *req_conf,
    auth_http_params *params)
{
	char *req_data     = NULL;
	char *content_type = "application/x-www-form-urlencoded";
	char *headers      = NULL;

	for (size_t i = 0; i < req_conf->header_count; i++) {
		if (strcasecmp(req_conf->headers[i]->key, "Content-Type") ==
		    0) {
			content_type = req_conf->headers[i]->value;
			continue;
		}
		str_append(&headers, req_conf->headers[i]->key);
		str_append(&headers, ": ");
		str_append(&headers, req_conf->headers[i]->value);
		str_append(&headers, "\r\n");
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
		if (req_data[strlen(req_data) - 1] == '&') {
			req_data[strlen(req_data) - 1] = '\0';
		}
	}
	struct mg_str host = mg_url_host(req_conf->url);

	if (strcasecmp(req_conf->method, "post") == 0 ||
	    strcasecmp(req_conf->method, "put") == 0) {
		int content_length = strlen(req_data);
		mg_printf(c,
		    "POST %s HTTP/1.0\r\n"
		    "Host: %.*s:%hu\r\n"
		    "%s"
		    "Content-Type: %s\r\n"
		    "Content-Length: %d\r\n"
		    "\r\n",
		    mg_url_uri(req_conf->url), (int) host.len, host.ptr,
		    mg_url_port(req_conf->url), headers == NULL ? "" : headers,
		    content_type, content_length);
		mg_send(c, req_data, content_length);
	} else if (strcasecmp(req_conf->method, "get") == 0) {
		mg_printf(c,
		    "GET %s?%s HTTP/1.1\r\n"
		    "Host: %.*s:%hu\r\n"
		    "%s"
		    "Content-Type: %s\r\n"
		    "\r\n",
		    mg_url_uri(req_conf->url), req_data, (int) host.len,
		    host.ptr, mg_url_port(req_conf->url),
		    headers == NULL ? "" : headers, content_type);
		mg_send(c, NULL, 0);
	}
	if (req_data) {
		free(req_data);
	}
	if (headers) {
		free(headers);
	}
}

static void
http_req_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data)
{
	struct mg_http_message *hm;
	comm_data *             data = fn_data;
	switch (ev) {
	case MG_EV_OPEN:
		*(uint64_t *) c->label =
		    mg_millis() + (data->config->connect_timeout * 1000);
		break;

	case MG_EV_POLL:
		if (mg_millis() > *(uint64_t *) c->label &&
		    (c->is_connecting || c->is_resolving)) {
			mg_error(c, "Connect timeout");
			data->status = 408;
		}
		break;

	case MG_EV_CONNECT:
		send_msg(c, data->req, data->params);
		break;

	case MG_EV_HTTP_MSG:
		hm = (struct mg_http_message *) ev_data;
		c->is_closing = 1; // Tell mongoose to close this
		data->status  = mg_http_status(hm);
		break;

	case MG_EV_ERROR:
		mg_error(c, "Request error");
		data->status = 400;
		break;

	default:
		break;
	}
}

int
http_auth_request(conf_auth_http *config, auth_http_params *params)
{
	comm_data fn_data = {
		.status = 0,
		.config = config,
		.params = params,
	};

	if (!config->enable) {
		return 200;
	}

	for (auth_type type = AUTH_REQ; type < REQ_COUNT; type++) {
		switch (type) {
		case AUTH_REQ:
			fn_data.req = &config->auth_req;
			/* code */
			break;
		case SUPER_REQ:
			fn_data.req = &config->super_req;
			/* code */
			break;
		case ACL_REQ:
			fn_data.req = &config->acl_req;
			/* code */
			break;
		default:
			break;
		}
		if (!fn_data.req->url) {
			continue;
		}
		fn_data.status = 0;
		struct mg_mgr mgr;
		mg_mgr_init(&mgr);
		mg_http_connect(&mgr, fn_data.req->url, http_req_cb,
		    &fn_data); // Create client connection

		while (fn_data.status == 0) {
			mg_mgr_poll(&mgr, 50);
		}
		mg_mgr_free(&mgr);
		if (200 != fn_data.status) {
			break;
		}
	}

	return fn_data.status;
}

int
verify_connect_by_http(conn_param *cparam, conf_auth_http *config)
{
	auth_http_params auth_params = {

		.clientid = (const char *) conn_param_get_clientid(cparam),
		.username = (const char *) conn_param_get_username(cparam),
		.password = (const char *) conn_param_get_password(cparam),
		// TODO incompleted fields
		// .access = ,
		// .ipaddress = ,
		// .protocol = ,
		// .sockport = ,
		// .common = ,
		// .subject = ,
		// .topic = ,
	};

	int status = http_auth_request(config, &auth_params);

	return status == 200 ? SUCCESS : NOT_AUTHORIZED;
}