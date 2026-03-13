#ifndef NNG_TLS_SSL_TEE2_H
#define NNG_TLS_SSL_TEE2_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef TLS_EXTERN_SS_CERTS

#include "openssl/engine.h"

ENGINE *engine_init(const char *name, const char *path);
bool loading_owner_cert_from_engine(ENGINE *engine, SSL_CTX *ctx);
bool loading_owner_key_from_engine(ENGINE *engine, SSL_CTX *ctx);
bool get_engin_info_in_passwd(const char *passwd);
char * tee_get_engine_name();
char * tee_get_engine_path();

#endif // TLS_EXTERN_SS_CERTS

#endif
