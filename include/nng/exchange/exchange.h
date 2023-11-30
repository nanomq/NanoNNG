#ifndef EXCHANGE_H
#define EXCHANGE_H

#include <stddef.h>
#include "nng/supplemental/nanolib/ringbuffer.h"

#define EXCHANGE_NAME_LEN 100
#define TOPIC_NAME_LEN    100
#define RINGBUFFER_MAX    100

typedef struct exchange_s exchange_t;
struct exchange_s {
	char name[EXCHANGE_NAME_LEN];
	char topic[TOPIC_NAME_LEN];

	ringBuffer_t *rbs[RINGBUFFER_MAX];
	unsigned int rb_count;
};

int exchange_init(exchange_t **ex, char *name, char *topic,
				  unsigned int *rbsCaps, char **rbsName, unsigned int rbsCount);
int exchange_add_rb(exchange_t *ex, ringBuffer_t *rb);
int exchange_release(exchange_t *ex);
int exchange_handle_msg(exchange_t *ex, void *msg);
int exchange_get_ringBuffer(exchange_t *ex, char *rbName, ringBuffer_t **rb);

#endif
