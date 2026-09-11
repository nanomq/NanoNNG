//
// Copyright 2023 NanoMQ Team, Inc.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define nng_exchange_self                0
#define nng_exchange_self_name           "exchange-client"
#define nng_exchange_peer                0
#define nng_exchange_peer_name           "exchange-server"
#define nng_opt_exchange_add             "exchange-client-add"

#define NNG_EXCHANGE_SELF                0
#define NNG_EXCHANGE_SELF_NAME           "exchange-client"
#define NNG_EXCHANGE_PEER                0
#define NNG_EXCHANGE_PEER_NAME           "exchange-server"
#define NNG_OPT_EXCHANGE_BIND            "exchange-client-bind"
#define NNG_OPT_EXCHANGE_GET_EX_QUEUE    "exchange-client-get-ex-queue"
#define NNG_OPT_EXCHANGE_GET_RBMSGMAP    "exchange-client-get-rbmsgmap"
#define NNG_OPT_EXCHANGE_START_LIMIT_TIMER "exchange-client-start-limit-timer"

NNG_DECL int nng_exchange_client_open(nng_socket *sock);

#ifndef nng_exchange_open
#define nng_exchange_open nng_exchange_client_open
#endif

/* Callback used by pair0 "replay-" to publish one MQTT message into the broker.
 * Return 0 on success, <0 on failure. Registered by nanomq (e.g. nano_mqtt_publish_async). */
typedef int (*nng_exchange_mqtt_publish_fn)(const char *topic, const void *payload,
    uint32_t len, uint8_t qos, bool retain);

NNG_DECL void nng_exchange_set_mqtt_publish_fn(nng_exchange_mqtt_publish_fn fn);

/* Parsed form of: replay-<start_key>-<end_key>-<interval_ms>-<pub_topic> */
typedef struct nng_exchange_replay_cmd {
	uint64_t start_key;
	uint64_t end_key;
	uint64_t interval_ms;
	char    *pub_topic; /* owned; free with nng_exchange_replay_cmd_free */
} nng_exchange_replay_cmd;

/* Returns 0 on success, -1 on parse/alloc failure. */
NNG_DECL int  nng_exchange_replay_cmd_parse(const char *input, nng_exchange_replay_cmd *out);
NNG_DECL void nng_exchange_replay_cmd_free(nng_exchange_replay_cmd *cmd);

#ifdef __cplusplus
}
#endif

#endif // #define EXCHANGE_CLIENT_H
