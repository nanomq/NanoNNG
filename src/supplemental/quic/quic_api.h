#ifndef NNG_SUPP_QUIC_API_H
#define NNG_SUPP_QUIC_API_H

#define QUIC_HIGH_PRIOR_MSG (0x01)

#define QUIC_MAIN_STREAM (1)
#define QUIC_SUB_STREAM (0)

#define QUIC_IDLE_TIMEOUT_DEFAULT (90)
#define QUIC_KEEPALIVE_DEFAULT (60)

#include "core/nng_impl.h"
#include "nng/nng.h"

typedef struct quic_dialer quic_dialer;
typedef struct quic_listener quic_listener;

extern int nni_quic_dialer_alloc(nng_stream_dialer **, const nni_url *);
extern int nni_quic_listener_alloc(nng_stream_listener **, const nni_url *);

#endif
