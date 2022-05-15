#ifndef AUTH_HTTP_H
#define AUTH_HTTP_H

#include "conf.h"
#include "cJSON.h"
#include "mongoose.h"
#include "nng/nng.h"
#include "nng/protocol/mqtt/mqtt.h"

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
	const char *topic;
};

typedef struct auth_http_params auth_http_params;

extern int verify_connect_by_http(conn_param *cparam, conf_auth_http *config);

#endif // AUTH_HTTP_H
