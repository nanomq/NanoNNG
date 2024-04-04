#ifndef NNG_SUPP_ICEORYX_API_H
#define NNG_SUPP_ICEORYX_API_H

#define NANO_ICEORYX_RECVQ_LEN 32
#define NANO_ICEORYX_SZ_BYTES 4
#define NANO_ICEORYX_ID_BYTES 4

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
    void **msgp, nano_iceoryx_puber *puber, uint32_t id, nng_msg *msg);

extern void nano_iceoryx_write(nano_iceoryx_puber *puber, void *msg);
extern void nano_iceoryx_read(nano_iceoryx_suber *suber, void **msgp);

#endif
