#ifndef NNG_TLS_SSL_TEE_H
#define NNG_TLS_SSL_TEE_H

//#define DEBUG_PKI_LOCAL 1

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TLS_EXTERN_PRIVATE_KEY
int teeGetCA(char **);
#endif

#ifdef __cplusplus
}
#endif

#endif
