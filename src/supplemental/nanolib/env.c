#include "nng/supplemental/nanolib/env.h"
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/file.h"

static void
set_string_var(char **var, const char *env_str)
{
	char *env = NULL;

	if ((env = getenv(env_str)) != NULL) {
		if (*var) {
			free(*var);
			*var = NULL;
		}
		*var = nni_strdup(env);
	}

}

static void
set_int_var(int *var, const char *env_str)
{
	char *env = NULL;

	if ((env = getenv(env_str)) != NULL) {
		*var = atoi(env);
	}
}

static void
set_long_var(long *var, const char *env_str)
{
	char *env = NULL;

	if ((env = getenv(env_str)) != NULL) {
		*var = atol(env);
	}
}

static void
set_bool_var(bool *var, const char *env_str)
{
	char *env = NULL;

	if ((env = getenv(env_str)) != NULL) {
		*var = nni_strcasecmp(env, "true") == 0
		    ? true
		    : nni_strcasecmp(env, "yes") == 0;
	}
}

static void
set_data_from_path_var(void **var, const char *env_str)
{
	char *env = NULL;

	if ((env = getenv(env_str)) != NULL) {
		file_load_data(env, var);
	}
}

static void
set_auth_type(auth_type_t *var, const char *env_str)
{
	char *env = NULL;

	if ((env = getenv(env_str)) != NULL) {
		if (nni_strcasecmp(env, "basic") == 0) {
			*var = BASIC;
		} else if (nni_strcasecmp(env, "jwt") == 0) {
			*var = JWT;
		}
	}
}

#if defined(ENABLE_LOG)
static void
set_log_level(conf_log *log)
{
	char *level = NULL;
	set_string_var(&level, NANOMQ_LOG_LEVEL);
	if (level != NULL) {
		int rv = log_level_num(level);
		if (-1 != rv) {
			log->level = rv;
		}
	}
	nng_strfree(level);
}

static void
set_log_uds_addr(conf_log *log)
{
	char *uds_addr = NULL;
	set_string_var(&uds_addr, NANOMQ_LOG_UDS_ADDR);
	if (uds_addr != NULL) {
		log->uds_addr = nng_strdup(uds_addr);
	}
	nng_strfree(uds_addr);
}

static void
set_log_rotation_size(conf_log *log)
{
	char *size = NULL;
	set_string_var(&size, NANOMQ_LOG_ROTATION_SIZE);
	if (size) {
		size_t num      = 0;
		char   unit[10] = { 0 };
		int    res      = sscanf(size, "%zu%s", &num, unit);
		if (res == 2) {
			if (nni_strcasecmp(unit, "KB") == 0) {
				log->rotation_sz = num * 1024;
			} else if (nni_strcasecmp(unit, "MB") == 0) {
				log->rotation_sz = num * 1024 * 1024;
			} else if (nni_strcasecmp(unit, "GB") == 0) {
				log->rotation_sz = num * 1024 * 1024 * 1024;
			}
		}
	}
	nng_strfree(size);

}

static void
set_log_to(conf_log *log)
{
	char *log_to = NULL;
	set_string_var(&log_to, NANOMQ_LOG_TO);
	if (log_to) {
		if (strstr(log_to, "file")) {
			log->type |= LOG_TO_FILE;
		}
		if (strstr(log_to, "console")) {
			log->type |= LOG_TO_CONSOLE;
		}
		if (strstr(log_to, "syslog")) {
			log->type |= LOG_TO_SYSLOG;
		}
		if (strstr(log_to, "uds")) {
			log->type |= LOG_TO_UDS;
			if (log->uds_addr == NULL) {
				fprintf(stderr, "uds addr is NULL");
			}
		}
	}
	nng_strfree(log_to);
}
#endif

char *
read_env_vin()
{
	char *env_vin = NULL;
	set_string_var(&env_vin, NANOMQ_VIN);
	return env_vin;
}

char *
read_env_pid_file()
{
	char *pid_file = NULL;
	set_string_var(&pid_file, NANOMQ_PID_FILE);
	return pid_file;
}

void
read_env_conf(conf *config)
{
	set_string_var(&config->url, NANOMQ_BROKER_URL);
	set_bool_var(&config->enable, NANOMQ_TCP_ENABLE);
	set_bool_var(&config->daemon, NANOMQ_DAEMON);
	set_int_var(&config->num_taskq_thread, NANOMQ_NUM_TASKQ_THREAD);
	set_int_var(&config->max_taskq_thread, NANOMQ_MAX_TASKQ_THREAD);
	set_long_var((long *) &config->parallel, NANOMQ_PARALLEL);
	set_int_var(&config->property_size, NANOMQ_PROPERTY_SIZE);
	set_long_var(
	    (long *) &config->max_packet_size, NANOMQ_MAX_PACKET_SIZE);
	set_long_var((long *) &config->client_max_packet_size,
	    NANOMQ_CLIENT_MAX_PACKET_SIZE);
	set_int_var(&config->msq_len, NANOMQ_MSQ_LEN);
	set_long_var((long *) &config->qos_duration, NANOMQ_QOS_DURATION);
	set_bool_var(&config->allow_anonymous, NANOMQ_ALLOW_ANONYMOUS);
	set_bool_var(&config->websocket.enable, NANOMQ_WEBSOCKET_ENABLE);
	set_bool_var(&config->websocket.tls_enable, NANOMQ_WEBSOCKET_TLS_ENABLE);
	set_string_var(&config->websocket.url, NANOMQ_WEBSOCKET_URL);
	set_string_var(&config->websocket.tls_url, NANOMQ_WEBSOCKET_TLS_URL);
	set_bool_var(&config->http_server.enable, NANOMQ_HTTP_SERVER_ENABLE);
	set_string_var(&config->http_server.ip_addr, NANOMQ_HTTP_SERVER_URL);
	set_int_var(
	    (int *) &config->http_server.port, NANOMQ_HTTP_SERVER_PORT);
	set_long_var((long *) &config->http_server.parallel,
	    NANOMQ_HTTP_SERVER_PARALLEL);
	set_string_var(
	    &config->http_server.username, NANOMQ_HTTP_SERVER_USERNAME);
	set_string_var(
	    &config->http_server.password, NANOMQ_HTTP_SERVER_PASSWORD);
	set_string_var(
	    &config->http_server.password, NANOMQ_HTTP_SERVER_PASSWORD);
	set_auth_type(
	    &config->http_server.auth_type, NANOMQ_HTTP_SERVER_AUTH_TYPE);
	set_data_from_path_var((void **) &config->http_server.jwt.public_key,
	    NANOMQ_HTTP_SERVER_JWT_PUBLIC_KEYFILE);
	set_data_from_path_var((void **) &config->http_server.jwt.private_key,
	    NANOMQ_HTTP_SERVER_JWT_PRIVATE_KEYFILE);

	set_bool_var(&config->tls.enable, NANOMQ_TLS_ENABLE);
	set_string_var(&config->tls.url, NANOMQ_TLS_URL);
	set_string_var(&config->tls.cafile, NANOMQ_TLS_CA_CERT_PATH);
	set_string_var(&config->tls.certfile, NANOMQ_TLS_CERT_PATH);
	set_string_var(&config->tls.keyfile, NANOMQ_TLS_KEY_PATH);

	set_data_from_path_var(
	    (void **) &config->tls.ca, NANOMQ_TLS_CA_CERT_PATH);
	set_data_from_path_var(
	    (void **) &config->tls.cert, NANOMQ_TLS_CERT_PATH);
	set_data_from_path_var(
	    (void **) &config->tls.key, NANOMQ_TLS_KEY_PATH);

	set_string_var(&config->tls.key_password, NANOMQ_TLS_KEY_PASSWORD);

	set_bool_var(&config->tls.verify_peer, NANOMQ_TLS_VERIFY_PEER);
	set_bool_var(&config->tls.set_fail, NANOMQ_TLS_FAIL_IF_NO_PEER_CERT);


	// log env
#if defined(ENABLE_LOG)
	set_log_level(&config->log);
	set_log_rotation_size(&config->log);
	set_log_uds_addr(&config->log);
	set_log_to(&config->log);
	set_string_var(&config->log.dir, NANOMQ_LOG_DIR);
	set_string_var(&config->log.file, NANOMQ_LOG_FILE);
	set_long_var((long*) &config->log.rotation_count, NANOMQ_LOG_ROTATION_COUNT);

	log_debug("ENV %s: %s", NANOMQ_LOG_DIR ,config->log.dir);
	log_debug("ENV %s: %s", NANOMQ_LOG_FILE ,config->log.file);
	log_debug("ENV %s: %ld",NANOMQ_LOG_ROTATION_COUNT , config->log.rotation_count);
	log_debug("ENV %s: %ld",NANOMQ_LOG_ROTATION_SIZE , config->log.rotation_sz);
	log_debug("ENV %s: %d", NANOMQ_LOG_LEVEL ,config->log.type);
#endif

	set_string_var(&config->conf_file, NANOMQ_CONF_PATH);
	printf("Set new conf path from env: %s\n", config->conf_file);
}