#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H

#include "core/nng_impl.h"
#include "nng/exchange/exchange.h"
#define nng_exchange_self                0
#define nng_exchange_self_name           "exchange-client"
#define nng_exchange_peer                0
#define nng_exchange_peer_name           "exchange-server"
#define nng_opt_exchange_add             "exchange-client-add"

#define NNG_EXCHANGE_SELF                0
#define NNG_EXCHANGE_SELF_NAME           "exchange-client"
#define NNG_EXCHANGE_PEER                0
#define NNG_EXCHANGE_PEER_NAME           "exchange-server"
#define NNG_OPT_EXCHANGE_ADD             "exchange-client-add"
#define NNG_OPT_EXCHANGE_GET_EX_QUEUE    "exchange-client-get-ex-queue"
#define NNG_OPT_EXCHANGE_GET_RBMSGMAP    "exchange-client-get-rbmsgmap"

int nng_exchange_client_open(nng_socket *sock);

int exchange_queue_get_ringBuffer(nni_list *ex_queue,
								  char *rbName, ringBuffer_t **rb);

#endif
