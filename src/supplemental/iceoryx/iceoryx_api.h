#ifndef NNG_SUPP_ICEORYX_API_H
#define NNG_SUPP_ICEORYX_API_H

#define NANO_ICEORYX_SERVICE  "NanoMQ"

typedef struct nano_iceoryx_suber nano_iceoryx_suber;
typedef void   nano_iceoryx_listener;

extern int nano_iceoryx_init();
extern int nano_iceoryx_fini();

extern void nano_iceoryx_listener_alloc(nano_iceoryx_listener **);
extern void nano_iceoryx_listener_free(nano_iceoryx_listener *);

extern nano_iceoryx_suber *nano_iceoryx_suber_alloc(
    const char *const service_name, const char *const instance_name,
    const char *const event, nano_iceoryx_listener *listener);
extern void nano_iceoryx_suber_free(nano_iceoryx_suber *suber)

#endif
