#ifndef EXCHANGE_H
#define EXCHANGE_H

#include <stddef.h>
#include "core/nng_impl.h"
#include "nng/supplemental/nanolib/conf.h"

NNG_DECL int exchange_client_get_msg_by_key(void *arg, uint32_t key, nni_msg **msg);
NNG_DECL int exchange_client_get_msgs_by_key(void *arg, uint32_t key, uint32_t count, nng_msg ***list);

NNG_DECL int exchange_init(exchange_t **ex, char *name, char *topic,
 				  unsigned int *rbsCaps, char **rbsName, unsigned int rbsCount);
NNG_DECL int exchange_add_rb(exchange_t *ex, ringBuffer_t *rb);
NNG_DECL int exchange_release(exchange_t *ex);
NNG_DECL int exchange_handle_msg(exchange_t *ex, int key, void *msg, nng_aio *aio);
NNG_DECL int exchange_get_ringBuffer(exchange_t *ex, char *rbName, ringBuffer_t **rb);

#endif
