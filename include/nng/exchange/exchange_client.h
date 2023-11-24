#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H

#include "nng/exchange/exchange.h"
#define NNG_EXCHANGE_SELF 0
#define NNG_EXCHANGE_SELF_NAME "exchange-client"
#define NNG_EXCHANGE_PEER 0
#define NNG_EXCHANGE_PEER_NAME "exchange-server"
#define NNG_OPT_EXCHANGE_ADD "exchange-client-add"

typedef struct exchange_sock_s exchange_sock_t;
typedef struct exchange_node_s exchange_node_t;

struct exchange_node_s {
	exchange_t      *ex;
	exchange_sock_t *sock;
	nni_aio         saio;
	nni_lmq         send_messages;
	nni_list_node   exnode;
	bool            isBusy;
	nni_mtx         mtx;
};

struct exchange_sock_s {
	nni_mtx         mtx;
	nni_atomic_bool closed;
	nni_list        ex_queue;
};


int nng_exchange_client_open(nng_socket *sock);

#endif
