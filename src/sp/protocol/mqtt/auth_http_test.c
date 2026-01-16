#include "nng/protocol/mqtt/mqtt_parser.h"
#include <nuts.h>

static void conf_auth_http_init(conf_auth_http **conf)
{
	*conf = nng_alloc(sizeof(conf_auth_http));
	if (*conf == NULL) {
		return;
	}
	(*conf)->enable = true;
	(*conf)->acl_req.header_count = 0;
	(*conf)->auth_req.header_count = 0;

	return;
}

static void conn_param_init(conn_param **conn_param)
{
	conn_param_alloc(conn_param);
	if (conn_param == NULL) {
		return;
	}

	conn_param_set_clientid(*conn_param, "clientid");
	conn_param_set_username(*conn_param, "username");
	conn_param_set_password(*conn_param, "password");

	return;
}

void test_auth_http_connect(void)
{
	conf_auth_http *conf = NULL;
	conf_auth_http_init(&conf);
	NUTS_TRUE(conf != NULL);
	char *url = "http://127.0.0.1:8064/mqtt/auth";
	conf->auth_req.url = nng_alloc(strlen(url) + 1);
	nng_mtx_alloc(&conf->auth_req.mtx);
	strncpy(conf->auth_req.url, url, strlen(url));
	conf->auth_req.url[strlen(url)] = '\0';

	conn_param *conn_param = NULL;
	conn_param_init(&conn_param);
	NUTS_TRUE(conn_param != NULL);

	int rc = nmq_auth_http_connect(conn_param, conf);
	/* send_request will be failed */
	NUTS_TRUE(rc != 0);

	nng_mtx_free(conf->auth_req.mtx);
	nng_free(conf->auth_req.url, strlen(conf->auth_req.url) + 1);
	nng_free(conf, sizeof(conf_auth_http));
	conn_param_free(conn_param);

	return;
}

void test_auth_http_sub_pub(void)
{
	conf_auth_http *conf = NULL;
	conf_auth_http_init(&conf);
	NUTS_TRUE(conf != NULL);
	char *url = "http://10.1.0.1:8964/mqtt/acl";
	conf->super_req.enable = false;
	conf->acl_req.enable = true;
	conf->enable = true;
	conf->acl_req.url = nng_alloc(strlen(url) + 1);
	strncpy(conf->acl_req.url, url, strlen(url));
	conf->acl_req.url[strlen(url)] = '\0';
	conf->super_req.url = NULL;
	conf->connect_timeout = 1;
	conf->timeout = 1;

	conn_param *conn_param = NULL;
	conn_param_init(&conn_param);
	NUTS_TRUE(conn_param != NULL);
	nng_mtx_alloc(&conf->acl_req.mtx);
	NUTS_TRUE(conf->acl_req.mtx != NULL);
	nng_mtx_alloc(&conf->super_req.mtx);
	NUTS_TRUE(conf->super_req.mtx != NULL);
	nng_mtx_alloc(&conf->acl_cache_mtx);
	NUTS_TRUE(conf->acl_cache_mtx != NULL);

	topic_queue *tq = topic_queue_init("topic1", strlen("topic1"));

	/* handle pub */
	int rc = nmq_auth_http_sub_pub(conn_param, false, tq, conf);
	/* send_request will be failed */
	printf("rc %d\n", rc);
	NUTS_TRUE(rc == NOT_AUTHORIZED);

	topic_queue *tq2 = topic_queue_init("topic2", strlen("topic2"));
	tq->next = tq2;
	rc = nmq_auth_http_sub_pub(conn_param, true, tq, conf);
	/* send_request will be failed */
	printf("rc %d\n", rc);
	NUTS_TRUE(rc == NOT_AUTHORIZED);

	topic_queue_release(tq);
	nng_mtx_free(conf->acl_req.mtx);
	nng_mtx_free(conf->super_req.mtx);
	nng_free(conf->acl_req.url, strlen(conf->acl_req.url) + 1);
	nng_free(conf, sizeof(conf_auth_http));
	conn_param_free(conn_param);

	return;
}

NUTS_TESTS = {
	{ "auth_http_connect", test_auth_http_connect },
	{ "auth_http_sub_pub", test_auth_http_sub_pub },
	{ NULL, NULL },
};
