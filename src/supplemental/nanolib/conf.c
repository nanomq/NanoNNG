//
// Copyright 2021 NanoMQ Team, Inc. <jaylin@emqx.io> //
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng/supplemental/nanolib/conf.h"
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/hocon.h"
#include "nanolib.h"
#include <ctype.h>

static void conf_bridge_parse(conf *nanomq_conf, const char *path);
static void conf_aws_bridge_parse(conf *nanomq_conf, const char *path);
static void conf_bridge_init(conf_bridge *bridge);
static void conf_bridge_destroy(conf_bridge *bridge);
static void conf_bridge_node_parse_subs(conf_bridge_node *node,
    const char *path, const char *key_prefix, const char *name);
static void conf_bridge_user_property_destroy(
    conf_user_property **prop, size_t sz);
static void conf_bridge_subscription_properties_parse(conf_bridge_node *node,
    const char *path, const char *key_prefix, const char *name);
static void conf_bridge_connect_properties_parse(conf_bridge_node *node,
    const char *path, const char *key_prefix, const char *name);
static void conf_bridge_connect_will_properties_parse(conf_bridge_node *node,
    const char *path, const char *key_prefix, const char *name);
static void print_bridge_conf(conf_bridge *bridge, const char *prefix);
static void conf_auth_init(conf_auth *auth);
static void conf_auth_parse(conf_auth *auth, const char *path);
static void conf_auth_destroy(conf_auth *auth);
static void conf_auth_http_req_init(conf_auth_http_req *req);

static void conf_auth_http_parse(conf_auth_http *auth_http, const char *path);
static void conf_auth_http_destroy(conf_auth_http *auth_http);

static void               conf_web_hook_destroy(conf_web_hook *web_hook);
static conf_http_header **conf_parse_http_headers(
    const char *path, const char *key_prefix, size_t *count);
static void conf_sqlite_parse(
    conf_sqlite *sqlite, const char *path, const char *key_prefix);
static void conf_sqlite_destroy(conf_sqlite *sqlite);

static void conf_web_hook_parse(conf_web_hook *webhook, const char *path);
static void conf_web_hook_destroy(conf_web_hook *web_hook);

static void conf_log_init(conf_log *log);
static void conf_log_destroy(conf_log *log);
static void conf_log_parse(conf_log *log, const char *path);

#if defined(SUPP_RULE_ENGINE)
static void conf_rule_repub_parse(conf_rule *cr, char *path);
static void conf_rule_mysql_parse(conf_rule *cr, char *path);
static bool conf_rule_sqlite_parse(conf_rule *cr, char *path);
static void conf_rule_fdb_parse(conf_rule *cr, char *path);
static void conf_rule_parse(conf_rule *rule, const char *path);
#endif

static char *
strtrim(char *str, size_t len)
{
	char * dest  = calloc(1, len);
	size_t index = 0;

	for (size_t i = 0; i < len; i++) {
		if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n') {
			dest[index] = str[i];
			index++;
		}
	}
	return dest;
}

char *
strtrim_head_tail(char *str, size_t len)
{
	size_t head = 0, tail = 0;

	for (size_t i = 0; i < len; i++) {
		if (str[i] == ' ' || str[i] == '\t' || str[i] == '\n') {
			head++;
		} else {
			break;
		}
	}

	for (size_t i = len - 1; i != 0; i--) {
		if (str[i] == ' ' || str[i] == '\t' || str[i] == '\n') {
			tail++;
		} else {
			break;
		}
	}

	size_t dest_len = len - head - tail + 1;
	char * dest     = calloc(1, dest_len);
	strncpy(dest, str + head, dest_len - 1);

	return dest;
}

void
conf_update_var(const char *fpath, const char *key, uint8_t type, void *var)
{
	char varstr[50] = { 0 };
	switch (type) {
	case 0:
		// int
		snprintf(varstr, 50, "%d", *(int *) var);
		break;
	case 1:
		// uint8
		snprintf(varstr, 50, "%hhu", *(uint8_t *) var);
		break;
	case 2:
		// uint16
		snprintf(varstr, 50, "%hu", *(uint16_t *) var);
		break;
	case 3:
		// uint32
		snprintf(varstr, 50, "%u", *(uint32_t *) var);
		break;
	case 4:
		// uint64
		snprintf(varstr, 50, "%llu", *(uint64_t *) var);
		break;
	case 5:
		// long
		snprintf(varstr, 50, "%ld", *(long *) var);
		break;
	case 6:
		// double
		snprintf(varstr, 50, "%lf", *(double *) var);
		break;
	case 7:
		// bool
		snprintf(varstr, 50, "%s", (*(bool *) var) ? "true" : "false");
		break;
	default:
		return;
	}
	conf_update(fpath, key, varstr);
}

void
conf_update_var2(const char *fpath, const char *key1, const char *key2,
    const char *key3, uint8_t type, void *var)
{
	size_t sz  = strlen(key1) + strlen(key2) + strlen(key3) + 2;
	char * key = nni_zalloc(sz);
	snprintf(key, sz, "%s%s%s", key1, key2, key3);
	conf_update_var(fpath, key, type, var);
	nng_free(key, sz);
}

void
conf_update(const char *fpath, const char *key, char *value)
{
	char **linearray = NULL;
	int    count     = 0;
	if (fpath == NULL || value == NULL) {
		return;
	}
	size_t descstrlen = strlen(key) + strlen(value) + 3;
	char * deststr    = calloc(1, descstrlen);
	char * ptr        = NULL;
	FILE * fp         = fopen(fpath, "r+");
	char * line       = NULL;
	size_t len        = 0;
	bool   is_found   = false;
	if (fp) {
		snprintf(deststr, descstrlen, "%s=", key);
		while (nano_getline(&line, &len, fp) != -1) {
			linearray =
			    realloc(linearray, (count + 1) * (sizeof(char *)));
			if (linearray == NULL) {
				log_error("realloc fail");
			}
			ptr = strstr(line, deststr);
			if (ptr == line) {
				is_found = true;
				strcat(deststr, value);
				strcat(deststr, "\n");
				linearray[count] = nng_strdup(deststr);
			} else {
				linearray[count] = nng_strdup(line);
			}
			count++;
		}
		if (!is_found) {
			linearray =
			    realloc(linearray, (count + 1) * (sizeof(char *)));
			strcat(deststr, value);
			strcat(deststr, "\n");
			linearray[count] = nng_strdup(deststr);
			count++;
		}
		if (line) {
			free(line);
		}
	} else {
		log_error("Open file %s error", fpath);
	}

	if (deststr) {
		free(deststr);
	}

	rewind(fp);
	fflush(fp);
	fclose(fp);

	fp = fopen(fpath, "w");

	for (int i = 0; i < count; i++) {
		fwrite(linearray[i], 1, strlen(linearray[i]), fp);
		free((linearray[i]));
	}
	free(linearray);
	fclose(fp);
}

void
conf_update2(const char *fpath, const char *key1, const char *key2,
    const char *key3, char *value)
{
	size_t sz  = strlen(key1) + strlen(key2) + strlen(key3) + 2;
	char * key = nni_zalloc(sz);
	snprintf(key, sz, "%s%s%s", key1, key2, key3);
	conf_update(fpath, key, value);
	nng_free(key, sz);
}



static char *
get_conf_value(char *line, size_t len, const char *key)
{
	if (strlen(key) > len || len <= 0) {
		return NULL;
	}

	char *ptr = strstr(line, key);
	if(ptr == NULL) {
		return NULL;
	}

	char *pound = strstr(line, "#");
    
    if (pound != NULL && pound < ptr) {
        return NULL;
    }

	char *prefix = nni_zalloc(len);
	char *trim   = strtrim_head_tail(line, len);
	char *value  = nni_zalloc(len);
	int   match  = sscanf(trim, "%[^=]=%[^\n]s", prefix, value);
	char *res    = NULL;
	nni_strfree(trim);

	char *line_key = strtrim_head_tail(prefix, strlen(prefix));
	if (match == 2 && strcmp(line_key, key) == 0) {
		res = strtrim_head_tail(value, strlen(value));
	}
	nni_strfree(value);

	free(line_key);
	free(prefix);
	return res;
}

static char *
get_conf_value_with_prefix(
    char *line, size_t len, const char *prefix, const char *key)
{
	size_t sz  = strlen(prefix) + strlen(key) + 2;
	char * str = nni_zalloc(sz);
	snprintf(str, sz, "%s%s", prefix, key);
	char *value = get_conf_value(line, len, str);
	free(str);
	return value;
}

static char *
get_conf_value_with_prefix2(char *line, size_t len, const char *prefix,
    const char *name, const char *key)
{
	size_t prefix_sz = prefix ? strlen(prefix) : 0;
	size_t name_sz   = name ? strlen(name) : 0;
	size_t sz        = prefix_sz + name_sz + strlen(key) + 2;

	char *str = nni_zalloc(sz);
	snprintf(str, sz, "%s%s%s", prefix, name ? name : "", key ? key : "");
	char *value = get_conf_value(line, len, str);
	free(str);
	return value;
}

void
conf_tls_parse(
    conf_tls *tls, const char *path, const char *prefix1, const char *prefix2)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}
	char * line = NULL;
	size_t sz   = 0;

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value_with_prefix2(
		         line, sz, prefix1, prefix2, "tls.enable")) != NULL) {
			tls->enable = nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                prefix1, prefix2, "tls.url")) != NULL) {
			FREE_NONULL(tls->url);
			tls->url = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                prefix1, prefix2, "tls.key_password")) !=
		    NULL) {
			FREE_NONULL(tls->key_password);
			tls->key_password = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                prefix1, prefix2, "tls.keyfile")) != NULL) {
			FREE_NONULL(tls->key);
			FREE_NONULL(tls->keyfile);
			tls->keyfile = value;
			file_load_data(tls->keyfile, (void **) &tls->key);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                prefix1, prefix2, "tls.certfile")) != NULL) {
			FREE_NONULL(tls->cert);
			FREE_NONULL(tls->certfile);
			tls->certfile = value;
			file_load_data(tls->certfile, (void **) &tls->cert);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                prefix1, prefix2, "tls.cacertfile")) != NULL) {
			FREE_NONULL(tls->ca);
			FREE_NONULL(tls->cafile);
			tls->cafile = value;
			file_load_data(tls->cafile, (void **) &tls->ca);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                prefix1, prefix2, "tls.verify_peer")) !=
		    NULL) {
			tls->verify_peer = nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                prefix1, prefix2,
		                "tls.fail_if_no_peer_cert")) != NULL) {
			tls->set_fail = nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		}
		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}




static void
conf_basic_parse(conf *config, const char *path)
{
	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;

	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	int   n;
	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value(line, sz, "url")) != NULL) {
			FREE_NONULL(config->url);
			config->url = value;
		} else if ((value = get_conf_value(line, sz, "daemon")) !=
		    NULL) {
			config->daemon = nni_strcasecmp(value, "yes") == 0 ||
			    nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "num_taskq_thread")) != NULL) {
			n = atoi(value);
			if (n > 0)
				config->num_taskq_thread = atoi(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "max_taskq_thread")) != NULL) {
			n = atoi(value);
			if (n > 0)
				config->max_taskq_thread = n;
			nng_strfree(value);
		} else if ((value = get_conf_value(line, sz, "parallel")) !=
		    NULL) {
			n = atoi(value);
			if (n > 0)
				config->parallel = n;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "property_size")) != NULL) {
			config->property_size = atoi(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "keepalive_backoff")) != NULL) {
			config->backoff = atof(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "max_packet_size")) != NULL) {
			config->max_packet_size = atoi(value) * 1024;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "client_max_packet_size")) != NULL) {
			config->client_max_packet_size = atoi(value) * 1024;
			nng_strfree(value);
		} else if ((value = get_conf_value(line, sz, "msq_len")) !=
		    NULL) {
			config->msq_len = atoi(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "qos_duration")) != NULL) {
			config->qos_duration = atoi(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "allow_anonymous")) != NULL) {
			config->allow_anonymous =
			    nni_strcasecmp(value, "yes") == 0 ||
			    nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
#ifdef ACL_SUPP
		} else if ((value = get_conf_value(
		                line, sz, "acl_enable")) != NULL) {
			config->acl.enable =
			    nni_strcasecmp(value, "yes") == 0 ||
			    nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value(line, sz, "acl_nomatch")) !=
		    NULL) {
			config->acl_nomatch =
			    nni_strcasecmp(value, "allow") == 0 ? ACL_ALLOW
			                                        : ACL_DENY;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "enable_acl_cache")) != NULL) {
			config->enable_acl_cache =
			    nni_strcasecmp(value, "on") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "acl_cache_max_size")) != NULL) {
			config->acl_cache_max_size = (size_t) atol(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "acl_cache_ttl")) != NULL) {
			uint64_t ttl = 0;
			if (get_time(value, &ttl) == 0) {
				config->acl_cache_ttl = ttl;
			}
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "acl_deny_action")) != NULL) {
			if (nni_strcasecmp(value, "ignore") == 0) {
				config->acl_deny_action = ACL_IGNORE;
			} else if (nni_strcasecmp(value, "disconnect") == 0) {
				config->acl_deny_action = ACL_DISCONNECT;
			}
			nng_strfree(value);
#endif
		} else if ((value = get_conf_value(
		                line, sz, "enable_ipc_internal")) != NULL) {
			config->ipc_internal =
			    nni_strcasecmp(value, "yes") == 0 ||
			    nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "websocket.enable")) != NULL) {
			config->websocket.enable =
			    nni_strcasecmp(value, "yes") == 0 ||
			    nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "websocket.url")) != NULL) {
			FREE_NONULL(config->websocket.url);
			config->websocket.url = value;
		} else if ((value = get_conf_value(
		                line, sz, "websocket.tls_url")) != NULL) {
			FREE_NONULL(config->websocket.tls_url);
			config->websocket.tls_url = value;
		} else if ((value = get_conf_value(
		                line, sz, "http_server.enable")) != NULL) {
			config->http_server.enable =
			    nni_strcasecmp(value, "yes") == 0 ||
			    nni_strcasecmp(value, "true") == 0;
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "http_server.port")) != NULL) {
			config->http_server.port = atoi(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "http_server.parallel")) != NULL) {
			config->http_server.parallel = atol(value);
			nng_strfree(value);
		} else if ((value = get_conf_value(
		                line, sz, "http_server.username")) != NULL) {
			FREE_NONULL(config->http_server.username);
			config->http_server.username = value;
		} else if ((value = get_conf_value(
		                line, sz, "http_server.password")) != NULL) {
			FREE_NONULL(config->http_server.password);
			config->http_server.password = value;
		} else if ((value = get_conf_value(
		                line, sz, "http_server.auth_type")) != NULL) {
			if (nni_strcasecmp("basic", value) == 0) {
				config->http_server.auth_type = BASIC;
			} else if ((nni_strcasecmp("jwt", value) == 0)) {
				config->http_server.auth_type = JWT;
			} else {
				config->http_server.auth_type = NONE_AUTH;
			}
			nng_strfree(value);
		} else if ((value = get_conf_value(line, sz,
		                "http_server.jwt.public.keyfile")) != NULL) {
			FREE_NONULL(config->http_server.jwt.public_keyfile);
			FREE_NONULL(config->http_server.jwt.public_key);
			config->http_server.jwt.public_keyfile = value;
			if (file_load_data(
			        config->http_server.jwt.public_keyfile,
			        (void **) &config->http_server.jwt
			            .public_key) > 0) {
				config->http_server.jwt
				    .iss = (char *) nni_plat_file_basename(
				    config->http_server.jwt.public_keyfile);
				config->http_server.jwt.public_key_len =
				    strlen(config->http_server.jwt.public_key);
			}
		} else if ((value = get_conf_value(line, sz,
		                "http_server.jwt.private.keyfile")) != NULL) {
			FREE_NONULL(config->http_server.jwt.private_keyfile);
			FREE_NONULL(config->http_server.jwt.private_key);
			config->http_server.jwt.private_keyfile = value;
			if (file_load_data(
			        config->http_server.jwt.private_keyfile,
			        (void **) &config->http_server.jwt
			            .private_key) > 0) {
				config->http_server.jwt.private_key_len =
				    strlen(
				        config->http_server.jwt.private_key);
			}
		}
		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}

void
conf_parse(conf *nanomq_conf)
{
	const char *conf_path = nanomq_conf->conf_file;

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
	

	conf *config = nanomq_conf;
	conf_basic_parse(config, conf_path);
#ifdef ACL_SUPP
	conf_acl_parse(&config->acl, conf_path);
#endif
	conf_tls_parse(&config->tls, conf_path, "\0", "\0");
	conf_sqlite_parse(&config->sqlite, conf_path, "sqlite");
	conf_web_hook_parse(&config->web_hook, conf_path);
	conf_bridge_parse(config, conf_path);
	conf_aws_bridge_parse(config, conf_path);
#if defined(ENABLE_LOG)
	conf_log_parse(&config->log, conf_path);
#endif

#if defined(SUPP_RULE_ENGINE)
	conf_rule_parse(&config->rule_eng, conf_path);
#endif

	conf_auth_parse(&config->auths, conf_path);
	conf_auth_http_parse(&config->auth_http, conf_path);
}

static void
conf_log_init(conf_log *log)
{
	log->level = NNG_LOG_WARN;
	log->file  = NULL;
	log->dir   = NULL;
	log->type  = LOG_TO_CONSOLE;
	log->fp    = NULL;

	log->abs_path        = NULL;
	log->rotation_sz_str = NULL;
	log->rotation_sz     = 10 * 1024;
	log->rotation_count  = 5;
}

static void
conf_log_destroy(conf_log *log)
{
	log->level = NNG_LOG_WARN;
	if (log->fp) {
		fclose(log->fp);
		log->fp = NULL;
	}
	if (log->file) {
		nni_strfree(log->file);
	}
	if (log->dir) {
		nni_strfree(log->dir);
	}
	if (log->rotation_sz_str) {
		nni_strfree(log->rotation_sz_str);
	}
	if (log->abs_path) {
		nni_strfree(log->abs_path);
	}
	log->type           = LOG_TO_CONSOLE;
	log->rotation_count = 5;
	log->rotation_sz    = 10 * 1024;
}

static void
conf_log_parse(conf_log *log, const char *path)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}
	char *  line     = NULL;
	size_t  sz       = 0;
	int     rv       = 0;
	uint8_t log_type = 0;

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value(line, sz, "log.level")) != NULL) {
			rv = log_level_num(value);
			free(value);
			if (rv != -1) {
				log->level = rv;
			} else {
				log->level = NNG_LOG_ERROR;
			}
		} else if ((value = get_conf_value(line, sz, "log.file")) !=
		    NULL) {
			FREE_NONULL(log->file);
			log->file = value;
		} else if ((value = get_conf_value(line, sz, "log.dir")) !=
		    NULL) {
			FREE_NONULL(log->dir);
			log->dir = value;
		} else if ((value = get_conf_value(line, sz, "log.to")) !=
		    NULL) {
			char *tk = strtok(value, ",");
			while (tk != NULL) {
				if (nni_strcasecmp(tk, "file") == 0) {
					log_type |= LOG_TO_FILE;
				} else if (nni_strcasecmp(tk, "console") ==
				    0) {
					log_type |= LOG_TO_CONSOLE;
				} else if (nni_strcasecmp(tk, "syslog") == 0) {
					log_type |= LOG_TO_SYSLOG;
				}
				tk = strtok(NULL, ",");
			}
			free(value);
		} else if ((value = get_conf_value(line, sz, "log.rotation.size")) != 0) {
			log->rotation_sz_str = value;
			size_t num           = 0;
			char   unit[10]      = { 0 };
			int    res = sscanf(value, "%zu%s", &num, unit);
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
		} else if ((value = get_conf_value(line, sz, "log.rotation.count")) != 0) {
			log->rotation_count = atoi(value);
			free(value);
		}
		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}
	fclose(fp);

	log->type = log_type;
}

void
conf_tls_init(conf_tls *tls)
{
	tls->url          = NULL;
	tls->enable       = false;
	tls->cafile       = NULL;
	tls->certfile     = NULL;
	tls->keyfile      = NULL;
	tls->ca           = NULL;
	tls->cert         = NULL;
	tls->key          = NULL;
	tls->key_password = NULL;
	tls->set_fail     = false;
	tls->verify_peer  = false;
}

void
conf_tls_destroy(conf_tls *tls)
{
	if (tls->url) {
		free(tls->url);
		tls->url = NULL;
	}
	if (tls->cafile) {
		free(tls->cafile);
		tls->cafile = NULL;
	}
	if (tls->certfile) {
		free(tls->certfile);
		tls->certfile = NULL;
	}
	if (tls->keyfile) {
		free(tls->keyfile);
		tls->keyfile = NULL;
	}
	if (tls->key) {
		free(tls->key);
		tls->key = NULL;
	}
	if (tls->key_password) {
		free(tls->key_password);
		tls->key_password = NULL;
	}
	if (tls->cert) {
		free(tls->cert);
		tls->cert = NULL;
	}
	if (tls->ca) {
		free(tls->ca);
		tls->ca = NULL;
	}
}

static void
conf_auth_http_req_init(conf_auth_http_req *req)
{
	req->url          = NULL;
	req->method       = NULL;
	req->header_count = 0;
	req->headers      = NULL;
	req->param_count  = 0;
	req->params       = NULL;
	conf_tls_init(&req->tls);
}

static void
conf_sqlite_init(conf_sqlite *sqlite)
{
	sqlite->enable              = false;
	sqlite->disk_cache_size     = 102400;
	sqlite->mounted_file_path   = NULL;
	sqlite->flush_mem_threshold = 100;
	sqlite->resend_interval     = 5000;
}

#if defined(SUPP_RULE_ENGINE)
static void
conf_rule_init(conf_rule *rule_en)
{
	rule_en->option = 0;
	rule_en->rules  = NULL;
	memset(rule_en->rdb, 0, sizeof(void *) * 3);
}
#endif

void
conf_http_server_init(conf_http_server *http, uint16_t port)
{
	http->enable              = false;
	http->port                = port;
	http->parallel            = 32;
	http->username            = NULL;
	http->password            = NULL;
	http->auth_type           = BASIC;
	http->jwt.iss             = NULL;
	http->jwt.private_key     = NULL;
	http->jwt.public_key      = NULL;
	http->jwt.private_keyfile = NULL;
	http->jwt.public_keyfile  = NULL;
}

void
conf_http_server_destroy(conf_http_server *http)
{
	nng_strfree(http->username);
	nng_strfree(http->password);

	nng_strfree(http->jwt.private_key);
	nng_strfree(http->jwt.public_key);
	nng_strfree(http->jwt.private_keyfile);
	nng_strfree(http->jwt.public_keyfile);
}

void
conf_init(conf *nanomq_conf)
{
	nanomq_conf->url       = NULL;
	nanomq_conf->conf_file = NULL;

#if defined(SUPP_RULE_ENGINE)
	conf_rule_init(&nanomq_conf->rule_eng);
#endif

	nanomq_conf->max_packet_size        = (10240 * 1024);
	nanomq_conf->client_max_packet_size = (10240 * 1024);

	int ncpu = nni_plat_ncpu();

	nanomq_conf->num_taskq_thread = ncpu * 2;
	nanomq_conf->max_taskq_thread = ncpu * 2;
	nanomq_conf->parallel         = ncpu * 2;

	nanomq_conf->property_size = sizeof(uint8_t) * 32;
	nanomq_conf->msq_len       = 2048;
	nanomq_conf->qos_duration  = 10;
	nanomq_conf->backoff       = 1.5;

	nanomq_conf->allow_anonymous = true;
	nanomq_conf->ipc_internal    = true;

#ifdef ACL_SUPP
	nanomq_conf->acl_nomatch        = ACL_ALLOW;
	nanomq_conf->enable_acl_cache   = true;
	nanomq_conf->acl_cache_max_size = 32;
	nanomq_conf->acl_cache_ttl      = 60;
	nanomq_conf->acl_deny_action    = ACL_IGNORE;
	conf_acl_init(&nanomq_conf->acl);
#endif
	nanomq_conf->daemon           = false;
	nanomq_conf->enable           = true;
	nanomq_conf->bridge_mode      = false;

#if defined(ENABLE_LOG)
	conf_log_init(&nanomq_conf->log);
#endif
	conf_sqlite_init(&nanomq_conf->sqlite);
	conf_tls_init(&nanomq_conf->tls);

	conf_http_server_init(&nanomq_conf->http_server, 8081);

	nanomq_conf->websocket.enable  = true;
	nanomq_conf->websocket.url     = NULL;
	nanomq_conf->websocket.tls_url = NULL;

	conf_bridge_init(&nanomq_conf->bridge);
	conf_bridge_init(&nanomq_conf->aws_bridge);

	nanomq_conf->web_hook.enable         = false;
	nanomq_conf->web_hook.url            = NULL;
	nanomq_conf->web_hook.encode_payload = plain;
	nanomq_conf->web_hook.pool_size      = 32;
	nanomq_conf->web_hook.headers        = NULL;
	nanomq_conf->web_hook.header_count   = 0;
	nanomq_conf->web_hook.rules          = NULL;
	nanomq_conf->web_hook.rule_count     = 0;
	conf_tls_init(&nanomq_conf->web_hook.tls);

	conf_auth_init(&nanomq_conf->auths);
	nanomq_conf->auth_http.enable = false;
	conf_auth_http_req_init(&nanomq_conf->auth_http.auth_req);
	conf_auth_http_req_init(&nanomq_conf->auth_http.super_req);
	conf_auth_http_req_init(&nanomq_conf->auth_http.acl_req);
	nanomq_conf->auth_http.timeout         = 5;
	nanomq_conf->auth_http.connect_timeout = 5;
	nanomq_conf->auth_http.pool_size       = 32;
}

static void
print_auth_conf(conf_auth *auth)
{
	if (auth && auth->enable) {
		for (size_t i = 0; i < auth->count; i++) {
			log_info("[%d] username: %s", i, auth->usernames[i]);
		}
	}
}

static char *
get_http_req_val(http_param_type type)
{
	switch (type) {
	case ACCESS:
		return "access";
	case USERNAME:
		return "username";
	case CLIENTID:
		return "clientid";
	case IPADDRESS:
		return "ipaddress";
	case PROTOCOL:
		return "protocol";
	case PASSWORD:
		return "password";
	case SOCKPORT: // sockport of server accepte
		return "sockport";
	case COMMON_NAME: // common name of client TLS cer:
		return "common_name";
	case SUBJECT: // subject of client TLS cer
		return "subject";
	case TOPIC:
		return "topic";
	case MOUNTPOINT:
		return "mountpoint";
	default:
		return "error type";
	}
}

static void
print_auth_http_req(conf_auth_http_req *req, const char *prefix)
{
	log_info("%s_req_url:       %s", prefix, req->url);
	log_info("%s_req_method:    %s", prefix, req->method);
	for (size_t i = 0; i < req->header_count; i++) {
		log_info("%s_hearders:      %s: %s", prefix, req->headers[i]->key,
		    req->headers[i]->value);
	}
	for (size_t i = 0; i < req->param_count; i++) {
		char *type = get_http_req_val(req->params[i]->type);
		log_info("%s_params:        %s: %s", prefix, req->params[i]->name, type);
	}
}

static void
print_auth_http_conf(conf_auth_http *auth_http)
{
	if (auth_http && auth_http->enable) {
		conf_auth_http_req auth = auth_http->auth_req;
		print_auth_http_req(&auth_http->auth_req, "auth");
		print_auth_http_req(&auth_http->super_req, "super");
		print_auth_http_req(&auth_http->acl_req, "acl");
	}

}

static char *
get_http_auth_type(auth_type_t type)
{
	switch (type)
	{
	case BASIC:
		return "basic";
	case JWT:
		return "jwt";
	case NONE_AUTH:
		return "no_auth";
	default:
		return "error type";
	}
}

static char *
get_webhook_type(hook_payload_type type)
{
	switch (type)
	{
	case plain:
		return "plain";
	case base64:
		return "base64";
	case base62:
		return "base62";
	default:
		return "error type";
	}
}

static char *
get_webhook_event_type(webhook_event type)
{
	switch (type)
	{
	case CLIENT_CONNECT:
		return "client_connect";
	case CLIENT_CONNACK:
		return "client_connack";
	case CLIENT_CONNECTED:
		return "client_connected";
	case CLIENT_DISCONNECTED:
		return "client_disconnected";
	case CLIENT_SUBSCRIBE:
		return "client_subscribe";
	case CLIENT_UNSUBSCRIBE:
		return "client_unsubscribe";
	case SESSION_SUBSCRIBED:
		return "session_subscribed";
	case SESSION_UNSUBSCRIBED:
		return "session_unsubscribed";
	case SESSION_TERMINATED:
		return "session_terminated";
	case MESSAGE_PUBLISH:
		return "message_publish";
	case MESSAGE_DELIVERED:
		return "message_delivered";
	case MESSAGE_ACKED:
		return "message_acked";
	case UNKNOWN_EVENT:
	default:
		return "unknown_event";
	}
}


static void
print_webhook_conf(conf_web_hook *webhook)
{
	if (webhook->enable) {
		log_info("webhook url:       %s", webhook->url);
		log_info("webhook_hearders:");
		for (size_t i = 0; i < webhook->header_count; i++) {
			conf_http_header *header = webhook->headers[i];
			log_info("	%s: %s", header->key, header->value);
		}
		const char *encode_type =
		    get_webhook_type(webhook->encode_payload);
		log_info("webhook encoding:  %s", encode_type);
		log_info("webhook poll size: %d", webhook->pool_size);
		log_info("webhook rule:");
		for (size_t i = 0; i < webhook->rule_count; i++) {
			conf_web_hook_rule *rule = webhook->rules[i];
			const char         *event =
			    get_webhook_event_type(rule->event);
			log_info("[%d] event:          %s", i, event);
			log_info("[%d] action:         %s", i, rule->action);
			if (rule->topic) {
				log_info(
				    "[%d] topic:          %s", i, rule->topic);
			}
		}
	}
}

#if defined(SUPP_RULE_ENGINE)
static void
print_rule_engine_conf(conf_rule *rule_eng)
{
	if (rule_eng->option & RULE_ENG_SDB) {
		log_info("rule engine sqlite:");
		log_info("path:           %s", rule_eng->sqlite_db);
		rule *r = rule_eng->rules;
		for (size_t i = 0; i < cvector_size(r); i++) {
			if (r[i].forword_type == RULE_FORWORD_SQLITE) {
				log_info("[%d] sql:        %s", i, r[i].raw_sql);
				log_info("[%d] table:      %s", i, r[i].sqlite_table);
			}
		}
	}

	if (rule_eng->option & RULE_ENG_RPB) {
		log_info("rule engine repub:");
		rule *r = rule_eng->rules;
		for (size_t i = 0; i < cvector_size(r); i++) {
			if (r[i].forword_type == RULE_FORWORD_REPUB) {
				repub_t *repub = r[i].repub;
				log_info("[%d] sql:        %s", i, r[i].raw_sql);
				log_info("[%d] server:     %s", i, repub->address);
				log_info("[%d] topic:      %s", i, repub->topic);
				log_info("[%d] proto_ver:  %d", i, repub->proto_ver);
				log_info("[%d] clientid:   %s", i, repub->clientid);
				log_info("[%d] keepalive:  %d", i, repub->keepalive);
				log_info("[%d] clean start:%d", i, repub->clean_start);
				log_info("[%d] username:   %s", i, repub->username);
				log_info("[%d] password:   ******", i);

			}
		}
	}

	if (rule_eng->option & RULE_ENG_MDB) {
		log_info("rule engine mysql:");
		log_info("name:         %s", rule_eng->mysql_db);
		rule *r = rule_eng->rules;
		for (size_t i = 0; i < cvector_size(r); i++) {
			if (r[i].forword_type == RULE_FORWORD_MYSOL) {
				rule_mysql *mysql = r[i].mysql;
				log_info("[%d] sql:      %s", i, r[i].raw_sql);
				log_info("[%d] table:    %s", i, mysql->table);
				log_info("[%d] host:     %s", i, mysql->host);
				log_info("[%d] username: %s", i, mysql->username);
				log_info("[%d] password: ******", i);
			}
		}
	}
}
#endif

void
print_conf(conf *nanomq_conf)
{
	log_info("This NanoMQ instance configured as:");

	if (nanomq_conf->enable) {
		log_info("tcp url:                  %s ", nanomq_conf->url);
	}
	if (nanomq_conf->websocket.enable) {
		log_info("websocket url:            %s",
		    nanomq_conf->websocket.url);
	}
	if (nanomq_conf->websocket.tls_url) {
		log_info("websocket tls url:        %s",
		    nanomq_conf->websocket.tls_url);
	}
	if (nanomq_conf->tls.enable) {
		conf_tls tls = nanomq_conf->tls;
		log_info("tls url:                  %s", nanomq_conf->tls.url);
		log_info("tls key file:             %s", tls.keyfile);
		log_info("tls cert file:            %s", tls.certfile);
		log_info("tls cacert file:          %s", tls.cafile);
		log_info("tls verify peer:          %s",
		    nanomq_conf->tls.verify_peer ? "true" : "false");
		log_info("tls fail_if_no_peer_cert: %s",
		    nanomq_conf->tls.set_fail ? "true" : "false");
	}
	log_info("daemon:                   %s",
	    nanomq_conf->daemon ? "true" : "false");
	log_info(
	    "num_taskq_thread:         %d", nanomq_conf->num_taskq_thread);
	log_info(
	    "max_taskq_thread:         %d", nanomq_conf->max_taskq_thread);
	log_info("parallel:                 %u", nanomq_conf->parallel);
	log_info("property_size:            %d", nanomq_conf->property_size);
	log_info("max_packet_size:          %d", nanomq_conf->max_packet_size);
	log_info("client_max_packet_size:   %d",
	    nanomq_conf->client_max_packet_size);
	log_info("msq_len:                  %d", nanomq_conf->msq_len);
	log_info("qos_duration:             %d", nanomq_conf->qos_duration);
	log_info("keepalive_backoff:        %f", nanomq_conf->backoff);

	if (nanomq_conf->http_server.enable) {
		conf_http_server hs = nanomq_conf->http_server;
		log_info("http server port:         %d", hs.port);
		log_info("http server parallel:     %u", hs.parallel);
		log_info("http server username:     %s", hs.username);
		const char *type = get_http_auth_type(hs.auth_type);
		log_info("http server auth type:    %s", type);
		if (hs.jwt.private_keyfile) {
			log_info("http server jwt:");
			log_info("	private key file:     %s",
			    hs.jwt.private_keyfile);
		}

		if (hs.jwt.public_keyfile) {
			log_info("	public key file:      %s",
			    hs.jwt.public_keyfile);
		}
	}

	if (nanomq_conf->sqlite.enable) {
		conf_sqlite sql = nanomq_conf->sqlite;
		log_info("sqlite:");
		log_info(
		    "	disk_cache_size:      %ld", sql.disk_cache_size);
		if (sql.mounted_file_path) {
			log_info("	mounted_file_path:    %s",
			    sql.mounted_file_path);
		}
		log_info(
		    "	flush_mem_threshold:  %ld", sql.flush_mem_threshold);
		log_info(
		    "	resend_interval:      %ld", sql.resend_interval);
	}

	log_info("allow_anonymous:          %s",
	    nanomq_conf->allow_anonymous ? "true" : "false");

#ifdef ACL_SUPP
	log_info("acl_nomatch:              %s",
	    nanomq_conf->acl_nomatch == ACL_ALLOW ? "allow" : "deny");
	log_info("enable_acl_cache:         %s",
	    nanomq_conf->enable_acl_cache ? "on" : "off");
	log_info(
	    "acl_cache_max_size:       %d", nanomq_conf->acl_cache_max_size);
	log_info("acl_cache_ttl:            %d", nanomq_conf->acl_cache_ttl);
	log_info("acl_deny_action:          %s",
	    nanomq_conf->acl_deny_action == ACL_IGNORE ? "ignore"
	                                               : "disconnect");
	print_acl_conf(&nanomq_conf->acl);
#endif
	conf_auth *auth = &(nanomq_conf->auths);
	conf_auth_http *auth_http = &(nanomq_conf->auth_http);
	conf_web_hook *webhook = &(nanomq_conf->web_hook);
	print_auth_conf(auth);
	print_auth_http_conf(auth_http);
	print_webhook_conf(webhook);
	print_bridge_conf(&nanomq_conf->bridge, "");
#if defined(SUPP_AWS_BRIDGE)
	print_bridge_conf(&nanomq_conf->aws_bridge, "aws.");
#endif

#if defined(SUPP_RULE_ENGINE)
	conf_rule *rule_eng = &(nanomq_conf->rule_eng);
	print_rule_engine_conf(rule_eng);
#endif
}

static void
conf_auth_init(conf_auth *auth)
{
	auth->count     = 0;
	auth->usernames = NULL;
	auth->passwords = NULL;
}

static void
conf_auth_parse(conf_auth *auth, const char *path)
{
	char   name_key[256] = "";
	char   pass_key[256] = "";
	char * name          = NULL;
	char * pass          = NULL;
	size_t index         = 1;
	bool   get_name      = false;
	bool   get_pass      = false;
	char * line          = NULL;
	size_t sz       = 0;
	char * value;

	auth->count = 0;

	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	while (nano_getline(&line, &sz, fp) != -1) {
		snprintf(name_key, 256, "auth.%ld.login", index);
		if (!get_name &&
		    (value = get_conf_value(line, sz, name_key)) != NULL) {
			name     = value;
			get_name = true;
			goto check;
		}

		snprintf(pass_key, 256, "auth.%ld.password", index);
		if (!get_pass &&
		    (value = get_conf_value(line, sz, pass_key)) != NULL) {
			pass     = value;
			get_pass = true;
			goto check;
		}

		free(line);
		line = NULL;

	check:
		if (get_name && get_pass) {
			index++;
			auth->count++;
			auth->usernames = realloc(
			    auth->usernames, sizeof(char *) * auth->count);
			auth->passwords = realloc(
			    auth->passwords, sizeof(char *) * auth->count);
			
			auth->usernames[auth->count - 1] = name;
			auth->passwords[auth->count - 1] = pass;

			get_name = false;
			get_pass = false;
		}
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}

static void
conf_auth_destroy(conf_auth *auth)
{
	for (size_t i = 0; i < auth->count; i++) {
		free(auth->usernames[i]);
		free(auth->passwords[i]);
	}
	free(auth->usernames);
	free(auth->passwords);
	auth->count = 0;
}

static void
printf_gateway_conf(zmq_gateway_conf *gateway)
{
	log_info("zmq sub url: %s", gateway->zmq_sub_url);
	log_info("zmq pub url: %s", gateway->zmq_pub_url);
	log_info("zmq sub pre: %s", gateway->zmq_sub_pre);
	log_info("zmq pub pre: %s", gateway->zmq_pub_pre);
	log_info("mqtt url: %s", gateway->mqtt_url);
	log_info("mqtt sub url: %s", gateway->sub_topic);
	log_info("mqtt pub url: %s", gateway->pub_topic);
	log_info("mqtt username: %s", gateway->username);
	log_info("mqtt password: %s", gateway->password);
	log_info("mqtt proto version: %d", gateway->proto_ver);
	log_info("mqtt keepalive: %d", gateway->keepalive);
	log_info("mqtt clean start: %d", gateway->clean_start);
	log_info("mqtt parallel: %d", gateway->parallel);
}

#if defined(SUPP_RULE_ENGINE)
static void
conf_rule_repub_parse(conf_rule *cr, char *path)
{
	assert(path);
	if (path == NULL || !nano_file_exists(path)) {
		printf("Configure file [%s] not found or "
		       "unreadable\n",
		    path);
		return;
	}

	char *   line = NULL;
	size_t   sz   = 0;
	FILE *   fp;
	repub_t *repub = NNI_ALLOC_STRUCT(repub);

	if (NULL == (fp = fopen(path, "r"))) {
		log_debug("File %s open failed\n", path);
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if (0 == strncmp(line, "rule.repub", strlen("rule.repub"))) {

			int num = 0;
			if (strstr(line, "address")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.address", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						repub->address = value;
					}
				}
			} else if (strstr(line, "topic")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.topic", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						repub->topic = value;
					}
				}
			} else if (strstr(line, "proto_ver")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.proto_ver", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						repub->proto_ver = atoi(value);
						free(value);
					}
				}
			} else if (strstr(line, "clientid")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.clientid", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						repub->clientid = value;
					}
				}
			} else if (strstr(line, "username")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.username", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						repub->username = value;
					}
				}
			} else if (strstr(line, "password")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.password", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						repub->password = value;
					}
				}
			} else if (strstr(line, "clean_start")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.clean_start", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						if (!strcmp(value, "true")) {
							repub->clean_start =
							    true;
						} else if (!strcmp(value,
						               "false")) {
							repub->clean_start =
							    false;
						} else {
							log_error(
							    "Unsupport clean "
							    "start option!");
							exit(EXIT_FAILURE);
						}
						free(value);
					}
				}
			} else if (strstr(line, "keepalive")) {
				if (0 != sscanf(line, "rule.repub.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.repub.%d.keepalive", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						repub->keepalive = atoi(value);
						free(value);
					}
				}
			}

		} else if (0 ==
		    strncmp(line, "rule.event.publish",
		        strlen("rule.event.publish"))) {

			// TODO more accurate way
			// topic <=======> broker <======> sql
			int num = 0;
			int res =
			    sscanf(line, "rule.event.publish.%d.sql", &num);
			if (0 == res) {
				log_error("Do not find repub client");
				exit(EXIT_FAILURE);
			}

			if (NULL != (value = strchr(line, '='))) {
				value++;
				rule_sql_parse(cr, value);
				char *p = strrchr(value, '\"');
				*p      = '\0';

				cr->rules[cvector_size(cr->rules) - 1].repub =
				    NNI_ALLOC_STRUCT(repub);
				memcpy(cr->rules[cvector_size(cr->rules) - 1]
				           .repub,
				    repub, sizeof(*repub));
				cr->rules[cvector_size(cr->rules) - 1]
				    .forword_type = RULE_FORWORD_REPUB;
				cr->rules[cvector_size(cr->rules) - 1]
				    .raw_sql = nng_strdup(++value);
				cr->rules[cvector_size(cr->rules) - 1]
				    .enabled = true;
				cr->rules[cvector_size(cr->rules) - 1]
				    .rule_id = rule_generate_rule_id();
			}
		}

		free(line);
		line = NULL;
	}

	NNI_FREE_STRUCT(repub);

	if (line) {
		free(line);
	}

	fclose(fp);
}

static void
conf_rule_mysql_parse(conf_rule *cr, char *path)
{
	assert(path);
	if (path == NULL || !nano_file_exists(path)) {
		printf("Configure file [%s] not found or "
		       "unreadable\n",
		    path);
		return;
	}

	char *      line = NULL;
	size_t      sz   = 0;
	FILE *      fp;
	rule_mysql *mysql = NNI_ALLOC_STRUCT(mysql);

	if (NULL == (fp = fopen(path, "r"))) {
		log_debug("File %s open failed\n", path);
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if (NULL !=
		    (value = get_conf_value(line, sz, "rule.mysql.name"))) {
			cr->mysql_db = value;
			log_debug(value);
		} else if (0 ==
		    strncmp(line, "rule.mysql", strlen("rule.mysql"))) {
			int num = 0;

			if (strstr(line, "table")) {
				if (0 != sscanf(line, "rule.mysql.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32,
					    "rule.mysql.%d.table", num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						log_debug(value);
						mysql->table = value;
					}
				}
			} else if (strstr(line, "host")) {
				if (0 != sscanf(line, "rule.mysql.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32, "rule.mysql.%d.host",
					    num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						log_debug(value);
						mysql->host = value;
					}
				}
			} else if (strstr(line, "username")) {
				if (0 != sscanf(line, "rule.mysql.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32, "rule.mysql.%d.username",
					    num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						log_debug(value);
						mysql->username = value;
					}
				}
			} else if (strstr(line, "password")) {
				if (0 != sscanf(line, "rule.mysql.%d", &num)) {
					char key[32] = { 0 };
					snprintf(key, 32, "rule.mysql.%d.password",
					    num);
					if (NULL !=
					    (value = get_conf_value(
					         line, sz, key))) {
						log_debug(value);
						mysql->password = value;
					}
				}
			}
		} else if (0 ==
		    strncmp(line, "rule.event.publish",
		        strlen("rule.event.publish"))) {

			// TODO more accurate way
			// topic <=======> broker <======> sql
			int num = 0;
			int res =
			    sscanf(line, "rule.event.publish.%d.sql", &num);
			if (0 == res) {
				log_error("Do not find mysql client");
				exit(EXIT_FAILURE);
			}

			if (NULL != (value = strchr(line, '='))) {
				value++;
				rule_sql_parse(cr, value);
				char *p = strrchr(value, '\"');
				*p      = '\0';

				cr->rules[cvector_size(cr->rules) - 1].mysql =
				    NNI_ALLOC_STRUCT(mysql);
				memcpy(cr->rules[cvector_size(cr->rules) - 1]
				           .mysql,
				    mysql, sizeof(*mysql));
				cr->rules[cvector_size(cr->rules) - 1]
				    .forword_type = RULE_FORWORD_MYSOL;
				cr->rules[cvector_size(cr->rules) - 1]
				    .raw_sql = nng_strdup(++value);
				cr->rules[cvector_size(cr->rules) - 1]
				    .enabled = true;
				cr->rules[cvector_size(cr->rules) - 1]
				    .rule_id = rule_generate_rule_id();
			}
		}

		free(line);
		line = NULL;
	}

	NNI_FREE_STRUCT(mysql);

	if (line) {
		free(line);
	}

	fclose(fp);
}

static bool
conf_rule_sqlite_parse(conf_rule *cr, char *path)
{
	assert(path);
	if (path == NULL || !nano_file_exists(path)) {
		log_debug("Configure file [%s] not found or "
		          "unreadable\n",
		    path);
		return;
	}

	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;
	char * table = NULL;

	if (NULL == (fp = fopen(path, "r"))) {
		log_error("File %s open failed\n", path);
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if (NULL !=
		    (value = get_conf_value(line, sz, "rule.sqlite.path"))) {
			cr->sqlite_db = value;
		} else if (NULL != strstr(line, "rule.sqlite")) {

			int num = 0;
			int res = sscanf(line, "rule.sqlite.%d.table", &num);
			if (0 == res) {
				log_fatal("Do not find table num");
				exit(EXIT_FAILURE);
			}

			char key[32] = { 0 };
			snprintf(key, 32, "rule.sqlite.%d.table", num);

			if (NULL != (value = get_conf_value(line, sz, key))) {
				table = value;
			}

		} else if (NULL != strstr(line, "rule.event.publish")) {

			// TODO more accurate way table <======> sql
			int num = 0;
			int res =
			    sscanf(line, "rule.event.publish.%d.sql", &num);
			if (0 == res) {
				log_fatal("Do not find table num");
				exit(EXIT_FAILURE);
			}

			if (NULL != (value = strchr(line, '='))) {
				value++;
				rule_sql_parse(cr, value);
				char *p = strrchr(value, '\"');
				*p      = '\0';

				cr->rules[cvector_size(cr->rules) - 1]
				    .sqlite_table = table;
				cr->rules[cvector_size(cr->rules) - 1]
				    .forword_type = RULE_FORWORD_SQLITE;
				cr->rules[cvector_size(cr->rules) - 1]
				    .raw_sql = nng_strdup(++value);
				cr->rules[cvector_size(cr->rules) - 1]
				    .enabled = true;
				cr->rules[cvector_size(cr->rules) - 1]
				    .rule_id = rule_generate_rule_id();
			}
		}

		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}

static void
conf_rule_fdb_parse(conf_rule *cr, char *path)
{
	if (path == NULL || !nano_file_exists(path)) {
		log_error("Configure file [%s] not found or "
		          "unreadable\n",
		    path);
		return;
	}

	char *    line = NULL;
	int       rc   = 0;
	size_t    sz   = 0;
	FILE *    fp;
	rule_key *rk = (rule_key *) nni_zalloc(sizeof(rule_key));
	memset(rk, 0, sizeof(rule_key));

	if (NULL == (fp = fopen(path, "r"))) {
		log_error("File %s open failed\n", path);
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value(line, sz, "rule.fdb.path")) !=
		    NULL) {
			cr->sqlite_db = value;
		} else if ((value = get_conf_value(
		                line, sz, "rule.event.publish.key")) != NULL) {
			if (-1 == (rc = rule_find_key(value, strlen(value)))) {
				if (strstr(value, "payload.")) {
					char *p = strchr(value, '.');
					rk->key_arr = NULL;
					rule_get_key_arr(p, rk);
					rk->flag[8] = true;
				}
			} else {
				rk->flag[rc] = true;
			}
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "rule.event.publish.key.autoincrement")) !=
		    NULL) {
			if (0 == nni_strcasecmp(value, "true")) {
				rk->auto_inc = true;
			} else if (0 == nni_strcasecmp(value, "false")) {
				rk->auto_inc = false;
			} else {
				log_warn("Unsupport autoincrement option.");
			}
			free(value);

		} else if (NULL != strstr(line, "rule.event.publish.sql")) {
			if (NULL != (value = strchr(line, '='))) {
				value++;
				rule_sql_parse(cr, value);
				char *p = strrchr(value, '\"');
				*p      = '\0';
				cr->rules[cvector_size(cr->rules) - 1].key =
				    rk;
				cr->rules[cvector_size(cr->rules) - 1]
				    .forword_type = RULE_FORWORD_FDB;
				cr->rules[cvector_size(cr->rules) - 1]
				    .raw_sql = nng_strdup(++value);
				cr->rules[cvector_size(cr->rules) - 1]
				    .enabled = true;
				cr->rules[cvector_size(cr->rules) - 1]
				    .rule_id = rule_generate_rule_id();
			}
		}

		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}

static void
conf_rule_parse(conf_rule *rule, const char *path)
{
	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;
	conf_rule *cr = rule;

	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed\n", path);
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value(line, sz, "rule_option")) !=
		    NULL) {
			if (0 != nni_strcasecmp(value, "ON")) {
				if (0 != nni_strcasecmp(value, "OFF")) {
					log_warn(
					    "Unsupported option: %s\nrule "
					    "option only support ON/OFF",
					    value);
				}
				free(value);
				break;
			}
			free(value);
			// sqlite
		} else if ((value = get_conf_value(
		                line, sz, "rule_option.sqlite")) != NULL) {
			if (0 == nni_strcasecmp(value, "enable")) {
				rule->option |= RULE_ENG_SDB;
			} else {
				if (0 != nni_strcasecmp(value, "disable")) {
					log_warn(
					    "Unsupported option: %s\nrule "
					    "option sqlite only support "
					    "enable/disable",
					    value);
					break;
				}
			}
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "rule_option.sqlite.conf.path")) != NULL) {
			if (RULE_ENG_SDB & rule->option) {
				conf_rule_sqlite_parse(&cr, value);
			}
			free(value);
			// repub
		} else if ((value = get_conf_value(
		                line, sz, "rule_option.repub")) != NULL) {
			if (0 == nni_strcasecmp(value, "enable")) {
				rule->option |= RULE_ENG_RPB;
			} else {
				if (0 != nni_strcasecmp(value, "disable")) {
					log_error(
					    "Unsupported option: %s\nrule "
					    "option sqlite only support "
					    "enable/disable",
					    value);
					break;
				}
			}
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "rule_option.repub.conf.path")) != NULL) {
			if (RULE_ENG_RPB & rule->option) {
				conf_rule_repub_parse(&cr, value);
			}
			free(value);
			// mysql
		} else if ((value = get_conf_value(
		                line, sz, "rule_option.mysql")) != NULL) {
			if (0 == nni_strcasecmp(value, "enable")) {
				rule->option |= RULE_ENG_MDB;
			} else {
				if (0 != nni_strcasecmp(value, "disable")) {
					log_warn(
					    "Unsupported option: %s\nrule "
					    "option mysql only support "
					    "enable/disable",
					    value);
					break;
				}
			}
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "rule_option.mysql.conf.path")) != NULL) {
			if (RULE_ENG_MDB & rule->option) {
				conf_rule_mysql_parse(&cr, value);
			}
			free(value);
			// fdb
		} else if ((value = get_conf_value(
		                line, sz, "rule_option.fdb")) != NULL) {
			if (0 == nni_strcasecmp(value, "enable")) {
				rule->option |= RULE_ENG_FDB;
			} else {
				if (0 != nni_strcasecmp(value, "disable")) {
					log_warn(
					    "Unsupported option: %s\nrule "
					    "option fdb only support "
					    "enable/disable",
					    value);
					break;
				}
			}
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "rule_option.fdb.conf.path")) != NULL) {
			if (RULE_ENG_FDB & rule->option) {
				conf_rule_fdb_parse(rule, value);
			}
			free(value);
		}

		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}
#endif

void
conf_gateway_parse(zmq_gateway_conf *gateway)
{
	const char *dest_path = gateway->path;

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

	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;

	if ((fp = fopen(dest_path, "r")) == NULL) {
		log_error("File %s open failed\n", dest_path);
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value(
		         line, sz, "gateway.mqtt.proto_ver")) != NULL) {
			gateway->proto_ver = atoi(value);
			free(value);
		} else if ((value = get_conf_value(
		                line, sz, "gateway.mqtt.keepalive")) != NULL) {
			gateway->keepalive = atoi(value);
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "gateway.mqtt.clean_start")) != NULL) {
			gateway->clean_start =
			    nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value(
		                line, sz, "gateway.mqtt.parallel")) != NULL) {
			gateway->parallel = atoi(value);
			free(value);
		} else if ((value = get_conf_value(
		                line, sz, "gateway.mqtt.address")) != NULL) {
			gateway->mqtt_url = value;
		} else if ((value = get_conf_value(line, sz,
		                "gateway.zmq.sub.address")) != NULL) {
			gateway->zmq_sub_url = value;
		} else if ((value = get_conf_value(line, sz,
		                "gateway.zmq.pub.address")) != NULL) {
			gateway->zmq_pub_url = value;
		} else if ((value = get_conf_value(
		                line, sz, "gateway.mqtt.username")) != NULL) {
			gateway->username = value;
		} else if ((value = get_conf_value(
		                line, sz, "gateway.mqtt.password")) != NULL) {
			gateway->password = value;
		} else if ((value = get_conf_value(
		                line, sz, "gateway.mqtt.forward")) != NULL) {
			gateway->pub_topic = value;
		} else if ((value = get_conf_value(
		                line, sz, "gateway.mqtt.forward")) != NULL) {
			gateway->pub_topic = value;
		} else if ((value = get_conf_value(line, sz,
		                "gateway.mqtt.subscription")) != NULL) {
			gateway->sub_topic = value;
		} else if ((value = get_conf_value(line, sz,
		                "gateway.mqtt.subscription")) != NULL) {
			gateway->sub_topic = value;
		} else if ((value = get_conf_value(
		                line, sz, "gateway.zmq.sub_pre")) != NULL) {
			gateway->zmq_sub_pre = value;
		} else if ((value = get_conf_value(
		                line, sz, "gateway.zmq.pub_pre")) != NULL) {
			gateway->zmq_pub_pre = value;
		}
		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}

	printf_gateway_conf(gateway);

	fclose(fp);
}

static conf_user_property **
conf_bridge_user_property_parse_ver2(
    const char *json, size_t json_len, size_t *sz)
{
	conf_user_property **ups = NULL;
	conf_user_property * up  = NULL;
	*sz                      = 0;

	cJSON *root = cJSON_ParseWithLength(json, json_len);
	if (cJSON_IsInvalid(root)) {
		return NULL;
	}

	cJSON *jso_item = NULL;
	cJSON_ArrayForEach(jso_item, root)
	{
		up        = NNI_ALLOC_STRUCT(up);
		up->key   = nni_strdup(jso_item->string);
		up->value = nni_strdup(jso_item->valuestring);
		cvector_push_back(ups, up);
	}

	*sz = cvector_size(ups);
	cJSON_Delete(root);
	return ups;
}

static void
conf_bridge_subscription_properties_parse(conf_bridge_node *node,
    const char *path, const char *key_prefix, const char *name)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	node->sub_properties = NNI_ALLOC_STRUCT(node->sub_properties);
	conf_bridge_sub_properties_init(node->sub_properties);
	conf_bridge_sub_properties *sub_prop = node->sub_properties;

	char * line         = NULL;
	size_t sz           = 0;
	char * value        = NULL;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value_with_prefix2(line, sz, key_prefix,
		         name, ".subscription.properties.identifier")) !=
		    NULL) {
			sub_prop->identifier = (uint32_t) atol(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".subscription.properties.user_property")) !=
		    NULL) {
			conf_bridge_user_property_destroy(
			    sub_prop->user_property,
			    sub_prop->user_property_size);

			sub_prop->user_property =
			    conf_bridge_user_property_parse_ver2(value,
			        strlen(value), &sub_prop->user_property_size);
			free(value);
		}

		if (line) {
			free(line);
			line = NULL;
		}
	}

	if (line) {
		free(line);
		line = NULL;
	}

	fclose(fp);
}

static void
conf_bridge_connect_will_properties_parse(conf_bridge_node *node,
    const char *path, const char *key_prefix, const char *name)
{
		FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	node->will_properties = NNI_ALLOC_STRUCT(node->will_properties);
	conf_bridge_conn_will_properties *will_prop = node->will_properties;

	char * line         = NULL;
	size_t sz           = 0;
	char * value        = NULL;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value_with_prefix2(line, sz, key_prefix,
		         name, ".will.properties.content_type")) != NULL) {
			will_prop->content_type = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".will.properties.response_topic")) != NULL) {
			will_prop->response_topic = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".will.properties.correlation_data")) !=
		    NULL) {
			will_prop->correlation_data = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".will.properties.message_expiry_interval")) !=
		    NULL) {
			will_prop->message_expiry_interval =
			    (uint32_t) atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".will.properties.payload_format_"
		                "indicator")) != NULL) {
			will_prop->payload_format_indicator =
			    (uint8_t) atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".will.properties.will_delay_interval")) !=
		    NULL) {
			will_prop->will_delay_interval =
			    (uint32_t) atol(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".will.properties.user_property")) != NULL) {
			conf_bridge_user_property_destroy(
			    will_prop->user_property,
			    will_prop->user_property_size);

			will_prop->user_property =
			    conf_bridge_user_property_parse_ver2(value,
			        strlen(value), &will_prop->user_property_size);
			free(value);
		}

		if (line) {
			free(line);
			line = NULL;
		}
	}

	if (line) {
		free(line);
		line = NULL;
	}

	fclose(fp);
}

static void
conf_bridge_connect_properties_parse(conf_bridge_node *node, const char *path,
    const char *key_prefix, const char *name)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	node->conn_properties = NNI_ALLOC_STRUCT(node->conn_properties);
	conf_bridge_conn_properties_init(node->conn_properties);
	conf_bridge_conn_properties *con_prop = node->conn_properties;

	char * line         = NULL;
	size_t sz           = 0;
	char * value        = NULL;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value_with_prefix2(line, sz, key_prefix,
		         name, ".connect.properties.maximum_packet_size")) !=
		    NULL) {
			con_prop->maximum_packet_size = (uint32_t) atol(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".connect.properties.receive_maximum")) !=
		    NULL) {
			con_prop->receive_maximum = (uint16_t) atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".connect.properties.topic_alias_maximum")) !=
		    NULL) {
			con_prop->topic_alias_maximum = (uint16_t) atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".connect.properties.request_problem_"
		                "infomation")) != NULL) {
			con_prop->request_problem_info = (uint8_t) atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".connect.properties.request_response_"
		                "infomation")) != NULL) {
			con_prop->request_response_info =
			    (uint8_t) atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".connect.properties.session_expiry_"
		                "interval")) != NULL) {
			con_prop->session_expiry_interval =
			    (uint32_t) atol(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name,
		                ".connect.properties.user_property")) !=
		    NULL) {
			conf_bridge_user_property_destroy(
			    con_prop->user_property,
			    con_prop->user_property_size);

			con_prop->user_property =
			    conf_bridge_user_property_parse_ver2(value,
			        strlen(value), &con_prop->user_property_size);
			free(value);
		}

		if (line) {
			free(line);
			line = NULL;
		}
	}

	if (line) {
		free(line);
		line = NULL;
	}

	fclose(fp);
}

static void
conf_bridge_node_parse_subs(
    conf_bridge_node *node, const char *path, const char *prefix, const char *name)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	char    topic_key[128] = "";
	char    qos_key[128]   = "";
	char *  topic          = NULL;
	uint8_t qos            = 0;
	size_t  sub_index      = 1;
	bool    get_topic      = false;
	bool    get_qos        = false;
	char *  line           = NULL;
	size_t  sz             = 0;
	char *  value          = NULL;

	node->sub_count = 0;
	while (nano_getline(&line, &sz, fp) != -1) {
		snprintf(topic_key, 128,
		    "%s%s.subscription.%ld.topic", prefix, name,
		    sub_index);
		if (!get_topic &&
		    (value = get_conf_value(line, sz, topic_key)) != NULL) {
			topic     = value;
			get_topic = true;
			goto check;
		}

		snprintf(qos_key, 128, "%s%s.subscription.%ld.qos", prefix,
		    name, sub_index);
		if (!get_qos &&
		    (value = get_conf_value(line, sz, qos_key)) != NULL) {
			qos = (uint8_t) atoi(value);
			free(value);
			get_qos = true;
			goto check;
		}

		free(line);
		line = NULL;

	check:
		if (get_topic && get_qos) {
			sub_index++;
			node->sub_count++;
			topics *s    = NNI_ALLOC_STRUCT(s);
			s->stream_id = 0;
			s->topic     = topic;
			s->topic_len = strlen(topic);
			s->qos       = qos;

#if defined(SUPP_QUIC)
			if (node->stream_auto_genid)
				s->stream_id = sub_index;
#endif
			cvector_push_back(node->sub_list, s);
			get_topic = false;
			get_qos   = false;
		}
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}

void
conf_bridge_sub_properties_init(conf_bridge_sub_properties *prop)
{
	prop->identifier         = 0xffffffff;
	prop->user_property      = NULL;
	prop->user_property_size = 0;
}

void
conf_bridge_conn_properties_init(conf_bridge_conn_properties *prop)
{
	prop->maximum_packet_size     = 0;
	prop->receive_maximum         = 65535;
	prop->topic_alias_maximum     = 0;
	prop->request_problem_info    = 1;
	prop->request_response_info   = 0;
	prop->session_expiry_interval = 0;
	prop->user_property_size      = 0;
	prop->user_property           = NULL;
}

void
conf_bridge_conn_will_properties_init(conf_bridge_conn_will_properties *prop)
{
	prop->content_type = NULL;
	prop->message_expiry_interval = 0;
	prop->payload_format_indicator = 0;
	prop->response_topic = NULL;
	prop->correlation_data = NULL;
	prop->user_property_size = 0;
	prop->user_property = NULL;
}

static void
conf_bridge_init(conf_bridge *bridge)
{
	bridge->count = 0;
	bridge->nodes = NULL;
	conf_sqlite_init(&bridge->sqlite);
}

void
conf_bridge_node_init(conf_bridge_node *node)
{
	node->sock           = NULL;
	node->name           = NULL;
	node->enable         = false;
	node->parallel       = 2;
	node->address        = NULL;
	node->host           = NULL;
	node->port           = 1883;
	node->clean_start    = true;
	node->clientid       = NULL;
	node->username       = NULL;
	node->password       = NULL;
	node->proto_ver      = 4;
	node->keepalive      = 60;
	node->forwards_count = 0;
	node->forwards       = NULL;
	node->sub_count      = 0;
	node->sub_list       = NULL;

	node->will_flag    = false;
	node->will_topic   = NULL;
	node->will_payload = NULL;
	node->will_qos     = 0;
	node->will_retain  = false;

	node->sqlite         = NULL;

	node->bridge_aio     = NULL;
	node->bridge_reload_aio = NULL;
	node->bridge_reload_aio2 = NULL;
	node->bridge_arg = NULL;

#if defined(SUPP_QUIC)
	node->multi_stream       = false;
	node->hybrid             = false;
	node->qkeepalive         = 30;
	node->qconnect_timeout   = 20; // HandshakeIdleTimeoutMs of QUIC
	node->qdiscon_timeout    = 20; // DisconnectTimeoutMs
	node->qidle_timeout      = 60; // Disconnect after idle
	node->qsend_idle_timeout = 2;
	node->qinitial_rtt_ms    = 800; // Ms
	node->qmax_ack_delay_ms  = 100;
	node->qcongestion_control = 1; // QUIC_CONGESTION_CONTROL_ALGORITHM_CUBIC
	node->quic_0rtt          = true;
#endif
	conf_tls_init(&node->tls);
	node->conn_properties = NULL;
	node->will_properties = NULL;
	node->sub_properties  = NULL;
}

static void
free_bridge_group_names(char **group_names, size_t n)
{
	if (group_names) {
		for (size_t i = 0; i < n; i++) {
			free(group_names[i]);
		}
		free(group_names);
		group_names = NULL;
	}
}

static char **
get_bridge_group_names(const char *path, const char *prefix, size_t *count)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return NULL;
	}

	size_t len     = strlen(prefix) + 34;
	char * pattern = nni_zalloc(len);
	snprintf(
	    pattern, len, "%sbridge.mqtt.%%[^.].%%*[^=]=%%*[^\n]", prefix);

	char * line = NULL;
	size_t sz      = 0;
	char **group_names = calloc(1, sizeof(char *));
	size_t group_count = 0;
	while (nano_getline(&line, &sz, fp) != -1) {
		char *value = calloc(1, sz);
		char *str   = strtrim_head_tail(line, sz);
		int   res   = sscanf(str, pattern, value);
		// avoid to read old version nanomq_bridge.conf
		if (res == 1 && strchr(value, '=') == NULL) {
			bool exists = false;
			for (size_t i = 0; i < group_count; i++) {
				if (strcmp(group_names[i], value) == 0) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				group_names = realloc(group_names,
				    sizeof(char *) * (group_count + 1));
				group_names[group_count] =
				    strtrim_head_tail(value, strlen(value));
				group_count++;
			}
		}
		if (value) {
			free(value);
		}
		free(str);
		free(line);
		line = NULL;
	}
	if (line) {
		free(line);
	}
	if (group_count == 0) {
		free(group_names);
		group_names = NULL;
	}
	nng_strfree(pattern);
	*count = group_count;
	return group_names;
}

static conf_bridge_node *
conf_bridge_node_parse_with_name(const char *path,const char *key_prefix, const char *name)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return NULL;
	}

	conf_bridge_node *node = calloc(1, sizeof(conf_bridge_node));
	nng_mtx_alloc(&node->mtx);
	conf_bridge_node_init(node);
	char * line         = NULL;
	size_t sz           = 0;
	char * value        = NULL;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value_with_prefix2(line, sz, key_prefix,
		         name, ".bridge_mode")) != NULL) {
			node->enable = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".proto_ver")) != NULL) {
			node->proto_ver = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".keepalive")) != NULL) {
			node->keepalive = atoi(value);
			free(value);
#if defined(SUPP_QUIC)
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_keepalive")) != NULL) {
			node->qkeepalive = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_idle_timeout")) != NULL) {
			node->qidle_timeout = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_discon_timeout")) != NULL) {
			node->qdiscon_timeout = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_handshake_timeout")) != NULL) {
			node->qconnect_timeout = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_send_idle_timeout")) != NULL) {
			node->qsend_idle_timeout = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_initial_rtt_ms")) != NULL) {
			node->qinitial_rtt_ms = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_max_ack_delay_ms")) != NULL) {
			node->qmax_ack_delay_ms = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".hybrid_bridging")) != NULL) {
			node->hybrid = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_multi_stream")) != NULL) {
			node->multi_stream = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_qos_priority")) != NULL) {
			node->qos_first = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_congestion_control")) !=
		    NULL) {
			if (0 == nng_strcasecmp(value, "bbr")) {
				node->qcongestion_control = 1;
			} else if (0 == nng_strcasecmp(value, "cubic")) {
				node->qcongestion_control = 0;
			} else {
				node->qcongestion_control = 1;
				log_warn("unsupport congestion control "
				         "algorithm, use "
				         "default bbr!");
			}
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".quic_0rtt")) != NULL) {
			node->quic_0rtt = nni_strcasecmp(value, "true") == 0;
			free(value);
#endif
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".clean_start")) != NULL) {
			node->clean_start = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".parallel")) != NULL) {
			node->parallel = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".max_send_queue_len")) != NULL) {
			node->max_send_queue_len = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".max_recv_queue_len")) != NULL) {
			node->max_recv_queue_len = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".address")) != NULL) {
			node->address = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".host")) != NULL) {
			node->host = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".port")) != NULL) {
			node->port = atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".clientid")) != NULL) {
			node->clientid = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".username")) != NULL) {
			node->username = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".password")) != NULL) {
			node->password = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".will.topic")) != NULL) {
			node->will_topic = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".will.payload")) != NULL) {
			node->will_payload = value;
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".will.retain")) != NULL) {
			node->will_retain = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".will.qos")) != NULL) {
			node->will_qos = (uint8_t) atoi(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix2(line, sz,
		                key_prefix, name, ".forwards")) != NULL) {
			char *tk = strtok(value, ",");
			while (tk != NULL) {
				node->forwards_count++;
				cvector_push_back(node->forwards, nng_strdup(tk));
				tk = strtok(NULL, ",");
			}
			free(value);
		}

		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}
	fclose(fp);
	if (node->will_topic != NULL && node->will_payload != NULL)
			node->will_flag = true;

	if (node->proto_ver == MQTT_PROTOCOL_VERSION_v5) {
		conf_bridge_connect_properties_parse(
		    node, path, key_prefix, name);
		if (node->will_topic != NULL && node->will_payload != NULL) {
			conf_bridge_connect_will_properties_parse(
			    node, path, key_prefix, name);
		}
		conf_bridge_subscription_properties_parse(
		    node, path, key_prefix, name);
	}

	conf_bridge_node_parse_subs(node, path, key_prefix, name);

	sz            = strlen(name) + 2;
	char *prefix2 = nng_zalloc(sz);
	snprintf(prefix2, sz, "%s.", name);
	conf_tls_parse(&node->tls, path, key_prefix, prefix2);
	nng_strfree(prefix2);

	return node;
}

static void
conf_bridge_content_parse(conf *nanomq_conf, conf_bridge *bridge,
    const char *prefix, const char *path)
{
	// 1. parse sqlite config from nanomq_bridge.conf
	size_t sz = strlen(prefix) + 15;
	char * key = nni_zalloc(sz);
	snprintf(key, sz, "%sbridge.sqlite", prefix);
	conf_sqlite_parse(&bridge->sqlite, path, "bridge.sqlite");
	nni_strfree(key);
	// 2. find all the name from the file
	size_t group_count;
	char **group_names =
	    get_bridge_group_names(path, prefix, &group_count);

	if (group_count == 0 || group_names == NULL) {
		log_debug("No bridge config group found");
		return;
	}

	// 3. foreach the names as the key, get the value from the file and set
	// sqlite config pointer;
	conf_bridge_node **node_array = NULL;
	cvector_set_size(node_array, sizeof(conf_bridge_node *) * group_count);

	char key_prefix[100] = {0};
	snprintf(key_prefix, 100, "%sbridge.mqtt.", prefix);

	bridge->count = group_count;
	for (size_t i = 0; i < group_count; i++) {
		conf_bridge_node *node = conf_bridge_node_parse_with_name(
		    path, key_prefix, group_names[i]);
		node->name    = nng_strdup(group_names[i]);
		node->sqlite  = &bridge->sqlite;
		cvector_push_back(node_array, node);
		nanomq_conf->bridge_mode |= node->enable;
	}
	bridge->nodes = node_array;
	free_bridge_group_names(group_names, group_count);
}

static void
conf_bridge_parse(conf *nanomq_conf, const char *path)
{
	conf_bridge *bridge = &nanomq_conf->bridge;
	conf_bridge_content_parse(nanomq_conf, bridge, "", path);
}

static void
conf_aws_bridge_parse(conf *nanomq_conf, const char *path)
{
#if defined(SUPP_AWS_BRIDGE)
	conf_bridge *bridge = &nanomq_conf->aws_bridge;
	conf_bridge_content_parse(nanomq_conf, bridge, "aws.", path);
#else
	NNI_ARG_UNUSED(nanomq_conf);
	NNI_ARG_UNUSED(path);
#endif
}

static void
conf_bridge_user_property_destroy(conf_user_property **prop, size_t sz)
{
	if (sz > 0 && prop) {
		for (size_t i = 0; i < sz; i++) {
			if (prop[i]) {
				if (prop[i]->key) {
					free(prop[i]->key);
				}
				if (prop[i]->value) {
					free(prop[i]->value);
				}
				free(prop[i]);
			}
		}
		cvector_free(prop);
		prop = NULL;
	}
}

void
conf_bridge_node_destroy(conf_bridge_node *node)
{
	node->enable = false;
	if (node->name) {
		free(node->name);
		node->name = NULL;
	}
	if (node->clientid) {
		free(node->clientid);
		node->clientid = NULL;
	}
	if (node->address) {
		free(node->address);
		node->address = NULL;
	}
	if (node->host) {
		free(node->host);
		node->host = NULL;
	}
	if (node->username) {
		free(node->username);
		node->username = NULL;
	}
	if (node->password) {
		free(node->password);
		node->password = NULL;

	}
	if (node->will_topic) {
		free(node->will_topic);
		node->will_topic = NULL;
	}
	if (node->will_payload) {
		free(node->will_payload);
		node->will_payload = NULL;
	}
	if (node->forwards_count > 0 && node->forwards) {
		for (size_t i = 0; i < node->forwards_count; i++) {
			if (node->forwards[i]) {
				free(node->forwards[i]);
				node->forwards[i] = NULL;
			}
		}
		cvector_free(node->forwards);
		node->forwards = NULL;
	}
	if (node->sub_count > 0 && node->sub_list) {
		for (size_t i = 0; i < node->sub_count; i++) {
			topics *s = node->sub_list[i];
			if (s->topic) {
				free(s->topic);
				s->topic = NULL;
			}
			NNI_FREE_STRUCT(s);
		}
		node->sub_count = 0;
		cvector_free(node->sub_list);
		node->sub_list = NULL;
	}
	if (node->conn_properties) {
		conf_bridge_user_property_destroy(
		    node->conn_properties->user_property,
		    node->conn_properties->user_property_size);
		node->conn_properties->user_property_size = 0;
		free(node->conn_properties);
		node->conn_properties = NULL;
	}
	if (node->will_properties) {
		conf_bridge_user_property_destroy(
		    node->will_properties->user_property,
		    node->will_properties->user_property_size);
		node->will_properties->user_property_size = 0;
		
		if (node->will_properties->content_type != NULL) {
			free(node->will_properties->content_type);
			node->will_properties->content_type = NULL;
		}
		if (node->will_properties->response_topic != NULL) {
			free(node->will_properties->response_topic);
			node->will_properties->response_topic = NULL;
		}
		if (node->will_properties->correlation_data != NULL) {
			free(node->will_properties->correlation_data);
			node->will_properties->correlation_data = NULL;
		}

		free(node->will_properties);
		node->will_properties =NULL;
	}
	if (node->sub_properties) {
		conf_bridge_user_property_destroy(
		    node->sub_properties->user_property,
		    node->sub_properties->user_property_size);
		node->sub_properties->user_property_size = 0;
		free(node->sub_properties);
		node->sub_properties = NULL;
	}
	conf_tls_destroy(&node->tls);
}

void
conf_bridge_destroy(conf_bridge *bridge)
{
	if (bridge->count > 0) {
		for (size_t i = 0; i < bridge->count; i++) {
			conf_bridge_node *node = bridge->nodes[i];
			conf_bridge_node_destroy(node);
			nng_mtx_free(node->mtx);
			free(node);
		}
		bridge->count = 0;
		cvector_free(bridge->nodes);
		bridge->nodes = NULL;
		conf_sqlite_destroy(&bridge->sqlite);
	}
}

static void
print_bridge_conf(conf_bridge *bridge, const char *prefix)
{
	if (bridge->count == 0 || bridge->nodes == NULL) {
		return;
	}
	for (size_t i = 0; i < bridge->count; i++) {
		conf_bridge_node *node = bridge->nodes[i];
		log_info("%sbridge.mqtt.%s.address:      %s", prefix,
		    node->name, node->address);
		log_info("%sbridge.mqtt.%s.proto_ver:    %d", prefix,
		    node->name, node->proto_ver);
		log_info("%sbridge.mqtt.%s.clientid:     %s", prefix,
		    node->name, node->clientid);
		log_info("%sbridge.mqtt.%s.clean_start:  %d", prefix,
		    node->name, node->clean_start);
		log_info("%sbridge.mqtt.%s.username:     %s", prefix,
		    node->name, node->username);
		log_info("%sbridge.mqtt.%s.password:     %s", prefix,
		    node->name, node->password);
		log_info("%sbridge.mqtt.%s.keepalive:    %d", prefix,
		    node->name, node->keepalive);
		log_info("%sbridge.mqtt.%s.parallel:     %ld", prefix,
		    node->name, node->parallel);
		log_info("%sbridge.mqtt.%s.tls.enable:   %s", prefix,
		    node->name, node->tls.enable ? "true" : "false");
		if (node->tls.enable) {
			log_info("tls:");
			log_info("	key file:         %s", node->tls.keyfile);
			log_info("	cert file:        %s", node->tls.certfile);
			log_info("	cacert file:      %s", node->tls.cafile);
		}
		log_info("%sbridge.mqtt.%s.forwards: ", prefix, node->name);

		for (size_t j = 0; j < node->forwards_count; j++) {
			log_info(
			    "\t[%ld] topic:        %s", j, node->forwards[j]);
		}
		log_info(
		    "%sbridge.mqtt.%s.subscription: ", prefix, node->name);
		for (size_t k = 0; k < node->sub_count; k++) {
			log_info("\t[%ld] topic:        %.*s", k + 1,
			    node->sub_list[k]->topic_len,
			    node->sub_list[k]->topic);
			log_info("\t[%ld] qos:          %d", k + 1,
			    node->sub_list[k]->qos);
		}
#if defined(SUPP_QUIC)
		log_info("%sbridge.mqtt.%s.quic_multi_stream:      %s", prefix,
		    node->name, node->multi_stream ? "true" : "false");
		log_info("%sbridge.mqtt.%s.quic_keepalive:         %ld", prefix,
		    node->name, node->qkeepalive);
		log_info("%sbridge.mqtt.%s.quic_handshake_timeout: %ld", prefix,
		    node->name, node->qconnect_timeout);
		log_info("%sbridge.mqtt.%s.quic_discon_timeout:    %ld", prefix,
		    node->name, node->qdiscon_timeout);
		log_info("%sbridge.mqtt.%s.qidle_timeout:          %ld", prefix,
		    node->name, node->qidle_timeout);
		log_info("%sbridge.mqtt.%s.qsend_idle_timeout:     %ld", prefix,
		    node->name, node->qsend_idle_timeout);
		log_info("%sbridge.mqtt.%s.qinitial_rtt_ms:        %ld", prefix,
		    node->name, node->qinitial_rtt_ms);
		log_info("%sbridge.mqtt.%s.qmax_ack_delay_ms:      %ld", prefix,
		    node->name, node->qinitial_rtt_ms);
		log_info("%sbridge.mqtt.%s.qcongestion_control:    %d", prefix,
		    node->name, node->qcongestion_control);
		log_info("%sbridge.mqtt.%s.quic_0rtt:              %s", prefix,
		    node->name, node->quic_0rtt ? "true" : "false");
#endif
	}

	if (bridge->sqlite.enable) {
		log_info("%sbridge.sqlite.disk_cache_size: %ld", prefix,
		    bridge->sqlite.disk_cache_size);
		log_info("%sbridge.sqlite.mounted_file_path: %s", prefix,
		    bridge->sqlite.mounted_file_path);
		log_info("%sbridge.sqlite.flush_mem_threshold: %ld", prefix,
		    bridge->sqlite.flush_mem_threshold);
		log_info("%sbridge.sqlite.resend_interval: %ld", prefix,
		    bridge->sqlite.resend_interval);
	}
}

webhook_event
get_webhook_event(const char *hook_type, const char *hook_name)
{
	if (nni_strcasecmp("client", hook_type) == 0) {
		if (nni_strcasecmp("connect", hook_name) == 0) {
			return CLIENT_CONNECT;
		} else if (nni_strcasecmp("connack", hook_name) == 0) {
			return CLIENT_CONNACK;
		} else if (nni_strcasecmp("connected", hook_name) == 0) {
			return CLIENT_CONNECTED;
		} else if (nni_strcasecmp("disconnected", hook_name) == 0) {
			return CLIENT_DISCONNECTED;
		} else if (nni_strcasecmp("subscribe", hook_name) == 0) {
			return CLIENT_SUBSCRIBE;
		} else if (nni_strcasecmp("unsubscribe", hook_name) == 0) {
			return CLIENT_UNSUBSCRIBE;
		}
	} else if (nni_strcasecmp("session", hook_type) == 0) {
		if (nni_strcasecmp("subscribed", hook_name) == 0) {
			return SESSION_SUBSCRIBED;
		} else if (nni_strcasecmp("unsubscribed", hook_name) == 0) {
			return SESSION_UNSUBSCRIBED;
		} else if (nni_strcasecmp("terminated", hook_name) == 0) {
			return SESSION_TERMINATED;
		}
	} else if (nni_strcasecmp("message", hook_type) == 0) {
		if (nni_strcasecmp("publish", hook_name) == 0) {
			return MESSAGE_PUBLISH;
		} else if (nni_strcasecmp("delivered", hook_name) == 0) {
			return MESSAGE_DELIVERED;
		} else if (nni_strcasecmp("acked", hook_name) == 0) {
			return MESSAGE_ACKED;
		}
	}
	return UNKNOWN_EVENT;
}

void
webhook_action_parse(const char *json, conf_web_hook_rule *hook_rule)
{
	cJSON *object = cJSON_Parse(json);

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

	cJSON_Delete(object);
}

static void
conf_web_hook_parse_rules(conf_web_hook *webhook, const char *path)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	char * line = NULL;
	size_t sz   = 0;

	webhook->rule_count = 0;
	while (nano_getline(&line, &sz, fp) != -1) {
		if (sz <= 20) {
			goto next;
		}
		char *   key      = calloc(1, sz - 20);
		char *   value    = calloc(1, sz - 20);
		char *   hooktype = NULL;
		char *   hookname = NULL;
		uint16_t num      = 0;
		char *   str      = strtrim_head_tail(line, sz);
		int      res =
		    sscanf(str, "web.hook.rule.%[^=]=%[^\n]", key, value);
		free(str);
		bool  match         = false;
		char *key_trimmed   = NULL;
		char *value_trimmed = NULL;
		if (res == 2) {
			key_trimmed = strtrim_head_tail(key, strlen(key));
			value_trimmed =
			    strtrim_head_tail(value, strlen(value));
			hooktype = calloc(1, strlen(key_trimmed));
			hookname = calloc(1, strlen(key_trimmed));
			res = sscanf(key_trimmed, "%[^.].%[^.].%hu", hooktype,
			    hookname, &num);
			if (res == 3) {
				match = true;
			}
		}
		if (match) {
			webhook->rule_count++;
			webhook->rules = realloc(webhook->rules,
			    webhook->rule_count *
			        (sizeof(conf_web_hook_rule *)));
			webhook->rules[webhook->rule_count - 1] =
			    calloc(1, sizeof(conf_web_hook_rule));
			webhook->rules[webhook->rule_count - 1]->event =
			    get_webhook_event(hooktype, hookname);
			webhook->rules[webhook->rule_count - 1]->rule_num =
			    num;
			webhook_action_parse(value_trimmed,
			    webhook->rules[webhook->rule_count - 1]);
		}
		if (key) {
			free(key);
		}
		if (value) {
			free(value);
		}
		if (key_trimmed) {
			free(key_trimmed);
		}
		if (value_trimmed) {
			free(value_trimmed);
		}
		if (hooktype) {
			free(hooktype);
		}
		if (hookname) {
			free(hookname);
		}
	next:
		free(line);
		line = NULL;
	}
	if (line) {
		free(line);
	}

	fclose(fp);
}

static void
conf_web_hook_parse(conf_web_hook *webhook, const char *path)
{
	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;

	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		webhook->enable = false;
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value(line, sz, "web.hook.enable")) !=
		    NULL) {
			webhook->enable = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value(
		                line, sz, "web.hook.url")) != NULL) {
			webhook->url = value;
		} else if ((value = get_conf_value(
		                line, sz, "web.hook.pool_size")) != NULL) {
			webhook->pool_size = (size_t) atol(value);
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "web.hook.body.encoding_of_payload_field")) !=
		    NULL) {
			if (nni_strcasecmp(value, "base64") == 0) {
				webhook->encode_payload = base64;
			} else if (nni_strcasecmp(value, "base62") == 0) {
				webhook->encode_payload = base62;
			} else if (nni_strcasecmp(value, "plain") == 0) {
				webhook->encode_payload = plain;
			}
			free(value);
		}
		free(line);
		line = NULL;
	}
	if (line) {
		free(line);
	}
	fclose(fp);

	webhook->headers =
	    conf_parse_http_headers(path, "web.hook", &webhook->header_count);
	conf_web_hook_parse_rules(webhook, path);
}

static void
conf_web_hook_destroy(conf_web_hook *web_hook)
{
	free(web_hook->url);

	if (web_hook->header_count > 0 && web_hook->headers != NULL) {
		for (size_t i = 0; i < web_hook->header_count; i++) {
			free(web_hook->headers[i]->key);
			free(web_hook->headers[i]->value);
			free(web_hook->headers[i]);
		}
		free(web_hook->headers);
	}

	if (web_hook->rule_count > 0 && web_hook->rules != NULL) {
		for (size_t i = 0; i < web_hook->rule_count; i++) {
			free(web_hook->rules[i]->action);
			free(web_hook->rules[i]->topic);
			free(web_hook->rules[i]);
		}
		free(web_hook->rules);
	}

	conf_tls_destroy(&web_hook->tls);
}

int
get_time(const char *str, uint64_t *second)
{
	char     unit = 0;
	uint64_t s    = 0;
	if (2 == sscanf(str, "%lld%c", &s, &unit)) {
		switch (unit) {
		case 's':
			*second = s;
			break;
		case 'm':
			*second = s * 60;
			break;
		case 'h':
			*second = s * 3600;
			break;
		// FIXME need to consider `ms` @ Xinyi
		default:
			break;
		}
		return 0;
	}
	return -1;
}

static conf_http_header **
conf_parse_http_headers(
    const char *path, const char *key_prefix, size_t *count)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return NULL;
	}

	char *             line    = NULL;
	size_t             sz      = 0;
	conf_http_header **headers = NULL;

	size_t len     = strlen(key_prefix) + 23;
	char * pattern = nni_zalloc(len);
	snprintf(pattern, len, "%s.headers.%%[^=]=%%[^\n]", key_prefix);

	size_t header_count = 0;
	while (nano_getline(&line, &sz, fp) != -1) {
		if (sz <= 16) {
			goto next;
		}
		char *key   = calloc(1, sz - 16);
		char *value = calloc(1, sz - 16);
		char *str   = strtrim_head_tail(line, sz);
		int   res   = sscanf(str, pattern, key, value);
		free(str);
		if (res == 2) {
			header_count++;
			headers = realloc(headers,
			    header_count * sizeof(conf_http_header *));
			headers[header_count - 1] =
			    calloc(1, sizeof(conf_http_header));
			headers[header_count - 1]->key =
			    strtrim_head_tail(key, strlen(key));
			headers[header_count - 1]->value =
			    strtrim_head_tail(value, strlen(value));
		}
		if (key) {
			free(key);
		}
		if (value) {
			free(value);
		}

	next:
		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}
	nng_strfree(pattern);
	fclose(fp);
	*count = header_count;
	return headers;
}

static conf_http_param **
get_params(const char *value, size_t *count)
{
	conf_http_param **params      = NULL;
	size_t            param_count = 0;

	char *line = nng_strdup(value);
	char *tk   = strtok(line, ",");
	while (tk != NULL) {
		param_count++;
		params =
		    realloc(params, sizeof(conf_http_param *) * param_count);
		char *str = nng_strdup(tk);
		char *key = calloc(1, strlen(str));
		char  c   = 0;
		int   res = sscanf(str, "%[^=]=%%%c", key, &c);
		if (res == 2) {
			params[param_count - 1] =
			    calloc(1, sizeof(conf_http_param));
			params[param_count - 1]->name = key;
			switch (c) {
			case 'A':
				params[param_count - 1]->type = ACCESS;
				break;
			case 'u':
				params[param_count - 1]->type = USERNAME;
				break;
			case 'c':
				params[param_count - 1]->type = CLIENTID;
				break;
			case 'a':
				params[param_count - 1]->type = IPADDRESS;
				break;
			case 'P':
				params[param_count - 1]->type = PASSWORD;
				break;
			case 'p':
				params[param_count - 1]->type = SOCKPORT;
				break;
			case 'C':
				params[param_count - 1]->type = COMMON_NAME;
				break;
			case 'd':
				params[param_count - 1]->type = SUBJECT;
				break;
			case 't':
				params[param_count - 1]->type = TOPIC;
				break;
			case 'm':
				params[param_count - 1]->type = MOUNTPOINT;
				break;
			case 'r':
				params[param_count - 1]->type = PROTOCOL;
				break;
			default:
				break;
			}
		} else {
			param_count--;
			if (key) {
				free(key);
			}
		}
		free(str);
		tk = strtok(NULL, ",");
	}
	if (line) {
		free(line);
	}
	*count = param_count;

	return params;
}

static void
conf_auth_http_req_parse(
    conf_auth_http_req *req, const char *path, const char *key_prefix)
{
	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}
	char * line = NULL;
	size_t sz   = 0;

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value_with_prefix(
		         line, sz, key_prefix, ".url")) != NULL) {
			req->url = value;
		} else if ((value = get_conf_value_with_prefix(
		                line, sz, key_prefix, ".method")) != NULL) {
			if (nni_strcasecmp(value, "post") == 0 ||
			    nni_strcasecmp(value, "get") == 0) {
				req->method = value;
			} else {
				free(value);
				req->method = nng_strdup("post");
			}
		} else if ((value = get_conf_value_with_prefix(
		                line, sz, key_prefix, ".params")) != NULL) {
			req->params = get_params(value, &req->param_count);
			free(value);
		}

		free(line);
		line = NULL;
	}

	if (line) {
		free(line);
	}
	fclose(fp);

	req->headers =
	    conf_parse_http_headers(path, key_prefix, &req->header_count);
}

static void
conf_auth_http_parse(conf_auth_http *auth_http, const char *path)
{
	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;

	if ((fp = fopen(path, "r")) == NULL) {
		log_debug("File %s open failed", path);
		auth_http->enable = false;
		return;
	}

	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value(line, sz, "auth.http.enable")) !=
		    NULL) {
			auth_http->enable = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value(
		                line, sz, "auth.http.timeout")) != NULL) {
			get_time(value, &auth_http->timeout);
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "auth.http.connect_timeout")) != NULL) {
			get_time(value, &auth_http->connect_timeout);
			free(value);
		} else if ((value = get_conf_value(line, sz,
		                "auth.http.connect_timeout")) != NULL) {
			get_time(value, &auth_http->connect_timeout);
			free(value);
		} else if ((value = get_conf_value(
		                line, sz, "auth.http.pool_size")) != NULL) {
			auth_http->pool_size = (size_t) atol(value);
			free(value);
		}

		free(line);
		line = NULL;
	}
	if (line) {
		free(line);
	}
	fclose(fp);

	conf_auth_http_req_parse(
	    &auth_http->auth_req, path, "auth.http.auth_req");
	conf_auth_http_req_parse(
	    &auth_http->super_req, path, "auth.http.super_req");
	conf_auth_http_req_parse(
	    &auth_http->acl_req, path, "auth.http.acl_req");
}

static void
conf_auth_http_req_destroy(conf_auth_http_req *req)
{
	if (req->url) {
		free(req->url);
	}
	if (req->method) {
		free(req->method);
	}
	if (req->header_count > 0 && req->headers != NULL) {
		for (size_t i = 0; i < req->header_count; i++) {
			free(req->headers[i]->key);
			free(req->headers[i]->value);
			free(req->headers[i]);
		}
		free(req->headers);
	}

	if (req->param_count > 0 && req->params != NULL) {
		for (size_t i = 0; i < req->param_count; i++) {
			free(req->params[i]->name);
			free(req->params[i]);
		}
		free(req->params);
	}
	conf_tls_destroy(&req->tls);
}

static void
conf_auth_http_destroy(conf_auth_http *auth_http)
{
	conf_auth_http_req_destroy(&auth_http->auth_req);
	conf_auth_http_req_destroy(&auth_http->super_req);
	conf_auth_http_req_destroy(&auth_http->acl_req);
}

static void
conf_sqlite_parse(
    conf_sqlite *sqlite, const char *path, const char *key_prefix)
{
	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;

	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		sqlite->enable = false;
		return;
	}
	char *value;
	while (nano_getline(&line, &sz, fp) != -1) {
		if ((value = get_conf_value_with_prefix(
		         line, sz, key_prefix, ".enable")) != NULL) {
			sqlite->enable = nni_strcasecmp(value, "true") == 0;
			free(value);
		} else if ((value = get_conf_value_with_prefix(line, sz,
		                key_prefix, ".disk_cache_size")) != NULL) {
			sqlite->disk_cache_size = (size_t) atol(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix(line, sz,
		                key_prefix, ".mounted_file_path")) != NULL) {
			sqlite->mounted_file_path = value;
		} else if ((value = get_conf_value_with_prefix(line, sz,
		                key_prefix, ".flush_mem_threshold")) != NULL) {
			sqlite->flush_mem_threshold = (size_t) atol(value);
			free(value);
		} else if ((value = get_conf_value_with_prefix(line, sz,
		                key_prefix, ".resend_interval")) != NULL) {
			sqlite->resend_interval = (uint64_t) atoll(value);
			free(value);
		}
		free(line);
		line = NULL;
	}
	if (line) {
		free(line);
	}
	fclose(fp);
}

static void
conf_sqlite_destroy(conf_sqlite *sqlite)
{
	if (sqlite->mounted_file_path) {
		free(sqlite->mounted_file_path);
	}
}

#if defined(SUPP_RULE_ENGINE)
static void
conf_rule_destroy(conf_rule *re)
{
	if (re) { }
}
#endif

void
conf_fini(conf *nanomq_conf)
{
	nng_strfree(nanomq_conf->url);
	nng_strfree(nanomq_conf->conf_file);
	nng_strfree(nanomq_conf->websocket.tls_url);

#if defined(SUPP_RULE_ENGINE)
	conf_rule_destroy(&nanomq_conf->rule_eng);
#endif
	conf_sqlite_destroy(&nanomq_conf->sqlite);
	conf_tls_destroy(&nanomq_conf->tls);

	nng_strfree(nanomq_conf->websocket.url);
#if defined(ACL_SUPP)
	conf_acl_destroy(&nanomq_conf->acl);
#endif
	conf_bridge_destroy(&nanomq_conf->bridge);
	conf_bridge_destroy(&nanomq_conf->aws_bridge);
	conf_web_hook_destroy(&nanomq_conf->web_hook);
	conf_auth_http_destroy(&nanomq_conf->auth_http);
	conf_auth_destroy(&nanomq_conf->auths);
#if defined(ENABLE_LOG)
	conf_log_destroy(&nanomq_conf->log);
#endif
	free(nanomq_conf);
}
