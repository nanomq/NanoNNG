//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef NNG_SUPP_ICEORYX_API_H
#define NNG_SUPP_ICEORYX_API_H

#include <stdint.h>

#include "nng/nng.h"

#define NANO_ICEORYX_RECVQ_LEN 32
#define NANO_ICEORYX_SZ_BYTES 4
#define NANO_ICEORYX_ID_BYTES 4
#define NANO_ICEORYX_MAX_NODES_NUMBER 32

typedef void   nano_iceoryx_listener;
typedef struct nano_iceoryx_suber nano_iceoryx_suber;
typedef struct nano_iceoryx_puber nano_iceoryx_puber;

extern int nano_iceoryx_init();
extern int nano_iceoryx_fini();

extern void nano_iceoryx_listener_alloc(nano_iceoryx_listener **);
extern void nano_iceoryx_listener_free(nano_iceoryx_listener *);

extern nano_iceoryx_suber *nano_iceoryx_suber_alloc(
    const char *subername, const char *const service_name,
    const char *const instance_name, const char *const event,
    nano_iceoryx_listener *listener);
extern void nano_iceoryx_suber_free(nano_iceoryx_suber *suber);

extern nano_iceoryx_puber *nano_iceoryx_puber_alloc(
    const char *pubername, const char *const service_name,
    const char *const instance_name, const char *const event);
extern void nano_iceoryx_puber_free(nano_iceoryx_puber *puber);

extern int nano_iceoryx_msg_alloc_raw(
    void **msgp, size_t sz, nano_iceoryx_puber *puber);
extern int nano_iceoryx_msg_alloc(
    nng_msg *msg, nano_iceoryx_puber *puber, uint32_t id);
extern void nano_iceoryx_msg_free(void *msg, nano_iceoryx_suber *suber);

extern void nano_iceoryx_write(nano_iceoryx_puber *puber, nng_aio *aio);
extern void nano_iceoryx_read(nano_iceoryx_suber *suber, nng_aio *aio);

#endif
