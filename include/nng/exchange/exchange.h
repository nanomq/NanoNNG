#ifndef EXCHANGE_H
#define EXCHANGE_H

#include <stddef.h>
#include "nng/exchange/producer.h"
#define EXCHANGE_NAME_LEN 100
#define PRODUCER_MAX      100

typedef struct exchange_s exchange_t;
struct exchange_s {
	char name[EXCHANGE_NAME_LEN];

	producer_t *prods[PRODUCER_MAX];
	unsigned int prods_count;
};

int exchange_init(exchange_t **ex, char *name);
int exchange_add_prod(exchange_t *ex, producer_t *prod);
int exchange_release(exchange_t *ex);
int exchange_handle_msg(exchange_t *ex, void *msg);

#endif
